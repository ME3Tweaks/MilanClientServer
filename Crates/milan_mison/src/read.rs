//! Zero-allocation deserialization.

use std::cell::Cell;
use std::fmt;
use std::marker::PhantomData;

use thiserror::Error;

use crate::MiType;
use crate::buffer::InputBuffer;
use crate::value::{MiValue, SerialValue};


/// Internal error variants for [`MiError`].
#[derive(Clone, Debug, Error)]
pub enum MiErrorKind {
    #[error("byte(s) can't represent this type: {reason}")]
    BadValue { reason: &'static str },
    #[error("needed {needed} byte(s), found {found}")]
    EndOfBuffer { needed: usize, found: usize },
    #[error("needed {needed} key type, found {found}")]
    KeyType { needed: MiType, found: MiType },
    #[error("needed {needed} value type, found {found}")]
    ValueType { needed: MiType, found: MiType },
}

/// Error produced by element / value deserialization.
#[derive(Clone, Debug, Error)]
pub struct MiError {
    pub kind: MiErrorKind,
    pub offset: Option<usize>,
}

impl MiError {
    pub fn bad_value(at: usize, reason: &'static str) -> Self {
        Self { kind: MiErrorKind::BadValue { reason }, offset: Some(at) }
    }

    pub fn end_of_buffer(at: usize, needed: usize, found: usize) -> Self {
        Self { kind: MiErrorKind::EndOfBuffer { needed, found }, offset: Some(at) }
    }

    pub fn key_type(needed: MiType, found: MiType) -> Self {
        Self { kind: MiErrorKind::KeyType { needed, found }, offset: None }
    }

    pub fn value_type(needed: MiType, found: MiType) -> Self {
        Self { kind: MiErrorKind::ValueType { needed, found }, offset: None }
    }
}

impl fmt::Display for MiError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.kind.fmt(f)?;
        if let Some(offset) = self.offset {
            write!(f, " (at offset 0x{:X})", offset)?;
        }
        Ok(())
    }
}


/// Result of iterative value access.
#[derive(Clone, Debug)]
pub enum MiReadResult<T> {
    /// Value successfully deserialized.
    Ok(T),
    /// Value failed to deserialize due to an error.
    Err(MiError),
    /// There can be no more values in this accessor.
    None,
}

impl<T> From<MiReadResult<T>> for Option<Result<T, MiError>> {
    fn from(value: MiReadResult<T>) -> Self {
        match value {
            MiReadResult::Ok(value) => Some(Ok(value)),
            MiReadResult::Err(error) => Some(Err(error)),
            MiReadResult::None => None,
        }
    }
}

impl<T> From<Option<Result<T, MiError>>> for MiReadResult<T> {
    fn from(value: Option<Result<T, MiError>>) -> Self {
        match value {
            Some(Ok(value)) => MiReadResult::Ok(value),
            Some(Err(error)) => MiReadResult::Err(error),
            None => MiReadResult::None,
        }
    }
}


/// Shallowly deserialized MISON element.
#[derive(Clone, Debug)]
pub struct MiElement<'a> {
    /// Original serialized offset.
    pub offset: usize,
    /// Element name, if present.
    pub name: Option<&'a str>,
    /// Value representation.
    pub value: MiValue<'a>,
}

impl<'a> MiElement<'a> {
    /// Checks if this element has no name.
    pub fn is_anonymous(&self) -> bool {
        self.name.is_none()
    }

    /// Checks if this element has a specified `name`.
    pub fn has_name(&self, name: &str) -> bool {
        self.name.is_some_and(|s| s == name)
    }

    /// Retrieves value type.
    pub fn get_type(&self) -> MiType {
        self.value.get_type()
    }

    /// Tries to deserialize an element from an `input` buffer.
    pub fn read_from<'f: 'a>(input: &'a InputBuffer<'f>) -> Result<Self, MiError> {
        let start = input.serial_offset();
        let tag = input.get_next_byte()?;

        let (mi_type, anonymous) = match MiType::split_tag(tag) {
            Some(pair) => { pair },
            None => Err(MiError::bad_value(input.pos() - 1, "invalid tag"))?,
        };

        let mut name = None;
        if !anonymous {
            let (name_pos, slice) = (input.pos(), input.get_next(8)?);
            match slice.iter().position(|&b| b == 0_u8) {
                None => Err(MiError::bad_value(name_pos, "non-terminated name"))?,
                Some(0) => Err(MiError::bad_value(name_pos, "empty name")) ?,
                Some(length) => match std::str::from_utf8(&slice[..length]) {
                    Err(_) => Err(MiError::bad_value(name_pos, "non-utf8 name"))?,
                    Ok(string) => name = Some(string),
                },
            };
        }

        let value = match mi_type {
            MiType::None => MiValue::None,
            MiType::Object => MiObjectView::read_from(input).map(MiValue::Object)?,
            MiType::Array => MiArrayErased::read_from(input).map(MiValue::Array)?,
            MiType::Map => MiMapErased::read_from(input).map(MiValue::Map)?,
            MiType::Int32 => i32::read_value(input).map(MiValue::Int32)?,
            MiType::Int64 => i64::read_value(input).map(MiValue::Int64)?,
            MiType::Single => f32::read_value(input).map(MiValue::Single)?,
            MiType::Double => f64::read_value(input).map(MiValue::Double)?,
            MiType::Bool => bool::read_value(input).map(MiValue::Bool)?,
            MiType::String => <&'a str>::read_value(input).map(MiValue::String)?,
            MiType::Binary => <&'a [u8]>::read_value(input).map(MiValue::Binary)?,
        };

        Ok(Self { offset: start, name, value })
    }
}


/// Accessor to a [`MiType::Object`] value.
///
/// # Remarks
/// Unlike [`MiArrayView`] and [`MiMapView`], this type has no "erased" alternative.
/// Those two types are collections and have associated generic item types, but the object
/// does not. Its payload is serialized as a sequence of *elements*, meaning that they
/// carry their types, and those types can differ. Thus, no need for type erasure.
#[derive(Clone, Debug)]
pub struct MiObjectView<'a> {
    count: usize,
    index: Cell<usize>,
    buffer: InputBuffer<'a>,
}

impl<'a> MiObjectView<'a> {
    /// Tries to deserialize an object `from` a slice.
    pub fn read_from<'f: 'a>(input: &'a InputBuffer<'f>) -> Result<Self, MiError> {
        let MiObjectHeader { size, count } = MiObjectHeader::read_from(input)?;
        let buffer = input.get_next_buffer(size as usize, 0)?;
        Ok(Self { count: count as usize, index: Cell::new(0), buffer })
    }

    /// Checks if the object is empty, i.e. has no elements.
    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Retrieves number of elements in the object.
    pub fn len(&self) -> usize {
        self.count
    }

    /// Checks if object iteration has reached end.
    pub fn at_end(&self) -> bool {
        self.index.get() == self.count
    }

    /// Resets object iteration to its initial state.
    pub fn reset(&self) {
        self.buffer.reset()
    }

    /// Tries to deserialize next element in the object.
    pub fn next_element<'f: 'a>(&'f self) -> MiReadResult<MiElement> {
        if self.at_end() {
            // Having non-empty remaining slice after `count` elements
            // is either a programming error on our side or mis-serialization.
            debug_assert_eq!(self.buffer.remaining(), 0);
            return MiReadResult::None;
        }

        let implement = || -> Result<MiElement, MiError> {
            let element = MiElement::read_from(&self.buffer)?;
            self.index.set(self.index.get() + 1);
            Ok(element)
        };

        Some(implement()).into()
    }
}

/// Deserialized object value header.
#[derive(Clone, Copy, Debug)]
pub struct MiObjectHeader {
    /// Number of bytes in the value, including this header.
    pub size: u32,
    /// Number of elements serialized in the object value.
    pub count: u16,
}

impl MiObjectHeader {
    /// Number of bytes [`Self::read_from`] should read from a slice.
    pub const SIZE: usize = 6;
    /// Signed number of bytes [`Self::read_from`] should read from a slice.
    pub const ISIZE: isize = Self::SIZE as isize;

    /// Tries to deserialize a header from an `input` buffer.
    pub fn read_from(input: &InputBuffer<'_>) -> Result<Self, MiError> {
        let size = input.get_next_array::<4>().map(u32::from_be_bytes)?;
        let count = input.get_next_array::<2>().map(u16::from_be_bytes)?;
        Ok(Self { size, count })
    }
}


/// Accessor to a [`MiType::Array`] value.
#[derive(Clone, Debug)]
pub struct MiArrayView<'a, Item>
where
    Item: SerialValue<'a>,
{
    count: usize,
    index: Cell<usize>,
    buffer: InputBuffer<'a>,
    _marker: PhantomData<&'a Item>,
}

impl<'a, Item> MiArrayView<'a, Item>
where
    Item: SerialValue<'a>,
{
    /// Tries to materialize a type-`erased` array into a typed array view.
    pub fn view<'s: 'a>(erased: MiArrayErased<'a>) -> Result<Self, MiError> {
        if erased.item_type != Item::MI_TYPE {
            Err(MiError::value_type(Item::MI_TYPE, erased.item_type))?;
        }

        Ok(Self {
            count: erased.count,
            index: Cell::new(0),
            buffer: erased.init_buffer,
            _marker: PhantomData,
        })
    }

    /// Tries to deserialize an array from an `input` buffer.
    pub fn read_from<'f: 'a>(input: &'a InputBuffer<'f>) -> Result<Self, MiError> {
        MiArrayErased::read_from(input).and_then(Self::view)
    }

    /// Checks if the array is empty, i.e. has no items.
    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Retrieves number of items in the array.
    pub fn len(&self) -> usize {
        self.count
    }

    /// Checks if array iteration has reached end.
    pub fn at_end(&self) -> bool {
        self.index.get() == self.count
    }

    /// Resets array iteration to its initial state.
    pub fn reset(&self) {
        self.buffer.reset()
    }

    /// Tries to parse next value in the array.
    pub fn next_value<'f: 'a>(&'f self) -> MiReadResult<Item> {
        if self.at_end() {
            // Having non-empty remaining slice after `count` elements
            // is either a programming error on our side or mis-serialization.
            debug_assert_eq!(self.buffer.remaining(), 0);
            return MiReadResult::None;
        }

        let implement = || -> Result<Item, MiError> {
            let value = Item::read_value(&self.buffer)?;
            self.index.set(self.index.get() + 1);
            Ok(value)
        };

        Some(implement()).into()
    }
}

/// Type-erased representation of an array value.
///
/// It must be [`materialize`]d into a [`MiArrayView`] before reading items.
/// This type is designed for two purposes: to allow reading array values when
/// the exact item type is not known at compile-time, and to expose a single type
/// for (de)serializing arrays via the [`SerialValue`] trait.
///
/// [`materialize`]: Self::materialize
#[derive(Clone, Debug)]
pub struct MiArrayErased<'a> {
    /// Number of items in the array.
    pub count: usize,
    /// Value type of items in the array.
    pub item_type: MiType,
    /// Initial payload buffer.
    pub init_buffer: InputBuffer<'a>,
}

impl<'a> MiArrayErased<'a> {
    /// Tries to deserialize an array from an `input` buffer.
    pub fn read_from<'f: 'a>(input: &'a InputBuffer<'f>) -> Result<Self, MiError> {
        let MiArrayHeader { size, count, item_type } = MiArrayHeader::read_from(input)?;
        let init_buffer = input.get_next_buffer(size as usize, 0)?;
        Ok(Self { count: count as usize, item_type, init_buffer })
    }

    /// Tries to materialize this type-erased array into a typed array view.
    pub fn materialize<Item>(&self) -> Result<MiArrayView<'a, Item>, MiError>
    where
        Item: SerialValue<'a>,
    {
        MiArrayView::<'a, Item>::view(self.clone())
    }
}

/// Deserialized array value header.
#[derive(Clone, Copy, Debug)]
pub struct MiArrayHeader {
    /// Number of bytes in the value, including this header.
    pub size: u32,
    /// Number of items serialized in the array.
    pub count: u16,
    /// Type of each item.
    pub item_type: MiType,
}

impl MiArrayHeader {
    /// Number of bytes [`Self::read_from`] should read from a slice.
    pub const SIZE: usize = 7;
    /// Signed number of bytes [`Self::read_from`] should read from a slice.
    pub const ISIZE: isize = Self::SIZE as isize;

    /// Tries to deserialize a header from an `input` buffer.
    pub fn read_from(input: &InputBuffer<'_>) -> Result<Self, MiError> {
        let size = input.get_next_array::<4>().map(u32::from_be_bytes)?;
        let count = input.get_next_array::<2>().map(u16::from_be_bytes)?;
        let item_type = MiType::from_tag(input.get_next_byte()?)
            .ok_or(MiError::bad_value(input.pos() - 1, "invalid type"))?;
        Ok(Self { size, count, item_type })
    }
}


/// Accessor to a [`MiType::Map`] value.
#[derive(Clone, Debug)]
pub struct MiMapView<'a, Key, Value>
where
    Key: SerialValue<'a>,
    Value: SerialValue<'a>,
{
    count: usize,
    index: Cell<usize>,
    buffer: InputBuffer<'a>,
    _marker: PhantomData<&'a (Key, Value)>,
}

impl<'a, Key, Value> MiMapView<'a, Key, Value>
where
    Key: SerialValue<'a>,
    Value: SerialValue<'a>,
{
    /// Tries to materialize an `erased` map into a typed map view.
    pub fn view<'s: 'a>(erased: MiMapErased<'a>) -> Result<Self, MiError> {
        match (erased.key_type, erased.value_type) {
            (key, value) if (key, value) == (Key::MI_TYPE, Value::MI_TYPE) =>
                Ok(Self {
                    count: erased.count,
                    index: Cell::new(0),
                    buffer: erased.init_buffer,
                    _marker: Default::default(),
                }),
            (_, value) if value == Value::MI_TYPE =>
                Err(MiError::key_type(Key::MI_TYPE, erased.key_type)),
            _ =>  // this handles "wrong key" and "both wrong" cases
                Err(MiError::value_type(Value::MI_TYPE, erased.value_type)),
        }
    }

    /// Tries to deserialize a map from an `input` buffer.
    pub fn read_from<'f: 'a>(input: &'a InputBuffer<'f>) -> Result<Self, MiError> {
        MiMapErased::read_from(input).and_then(Self::view)
    }

    /// Checks if the map is empty, i.e. has no items.
    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Retrieves number of pairs in the map.
    pub fn len(&self) -> usize {
        self.count
    }

    /// Checks if map iteration has reached end.
    pub fn at_end(&self) -> bool {
        self.index.get() == self.count
    }

    /// Resets map iteration to its initial state.
    pub fn reset(&self) {
        self.buffer.reset()
    }

    /// Tries to parse next pair in the map.
    pub fn next_pair<'f: 'a>(&'f self) -> MiReadResult<(Key, Value)> {
        if self.at_end() {
            // Having non-empty remaining slice after `count` elements
            // is either a programming error on our side or mis-serialization.
            debug_assert_eq!(self.buffer.remaining(), 0);
            return MiReadResult::None;
        }

        let implement = || -> Result<(Key, Value), MiError> {
            let key = Key::read_value(&self.buffer)?;
            let value = Value::read_value(&self.buffer)?;
            self.index.set(self.index.get() + 1);
            Ok((key, value))
        };

        Some(implement()).into()
    }
}

/// Type-erased representation of a map value.
///
/// # Remarks
/// See [`MiArrayErased`] for further explanation.
#[derive(Clone, Debug)]
pub struct MiMapErased<'a> {
    /// Number of pairs in the map.
    pub count: usize,
    /// Key type for pairs in the map.
    pub key_type: MiType,
    /// Value type for pairs in the map.
    pub value_type: MiType,
    /// Initial payload buffer.
    pub init_buffer: InputBuffer<'a>,
}

impl<'a> MiMapErased<'a> {
    /// Tries to deserialize a map from an `input` buffer.
    pub fn read_from<'f: 'a>(input: &'a InputBuffer<'f>) -> Result<Self, MiError> {
        let MiMapHeader { size, count, key_type, value_type } = MiMapHeader::read_from(input)?;
        let init_buffer = input.get_next_buffer(size as usize, 0)?;
        Ok(Self { count: count as usize, key_type, value_type, init_buffer })
    }

    /// Tries to materialize this type-erased map into a typed map view.
    pub fn materialize<Key, Value>(&self) -> Result<MiMapView<'a, Key, Value>, MiError>
    where
        Key: SerialValue<'a>,
        Value: SerialValue<'a>,
    {
        MiMapView::<'a, Key, Value>::view(self.clone())
    }
}

/// Deserialized map value header.
#[derive(Clone, Copy, Debug)]
pub struct MiMapHeader {
    /// Number of bytes in the value, including this header.
    pub size: u32,
    /// Number of pairs serialized in the map.
    pub count: u16,
    /// Item key type.
    pub key_type: MiType,
    /// Item value type.
    pub value_type: MiType,
}

impl MiMapHeader {
    /// Number of bytes [`Self::read_from`] should read from a slice.
    pub const SIZE: usize = 8;
    /// Signed number of bytes [`Self::read_from`] should read from a slice.
    pub const ISIZE: isize = Self::SIZE as isize;

    /// Tries to deserialize a header from an `input` buffer.
    pub fn read_from(input: &InputBuffer<'_>) -> Result<Self, MiError> {
        let size = input.get_next_array::<4>().map(u32::from_be_bytes)?;
        let count = input.get_next_array::<2>().map(u16::from_be_bytes)?;

        let key_type = MiType::from_tag(input.get_next_byte()?)
            .ok_or(MiError::bad_value(input.pos() - 1, "invalid key type"))?;
        let value_type = MiType::from_tag(input.get_next_byte()?)
            .ok_or(MiError::bad_value(input.pos() - 1, "invalid value type"))?;

        Ok(Self { size, count, key_type, value_type })
    }
}
