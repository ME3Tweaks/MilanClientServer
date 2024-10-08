//! Zero-allocation deserialization.

use std::cell::Cell;
use std::fmt;
use std::marker::PhantomData;

use thiserror::Error;

use crate::MiType;
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

    /// Tries to deserialize an element `from` a slice.
    pub fn read_from<'f: 'a>(from: &'f [u8], offset: &mut usize) -> Result<Self, MiError> {
        let element_start = *offset;

        let (mi_type, anonymous) = match from.first() {
            Some(&byte) => match MiType::split_tag(byte) {
                Some(pair) => { *offset += 1; pair },
                None => Err(MiError::bad_value(*offset, "invalid tag"))?,
            },
            None => Err(MiError::end_of_buffer(*offset, 1, 0))?,
        };

        let name = match anonymous {
            true => None,
            false => match from.get(1 .. 9) {
                Some(slice) => match slice.iter().position(|&b| b == 0_u8) {
                    None => Err(MiError::bad_value(*offset, "non-terminated name"))?,
                    Some(0) => Err(MiError::bad_value(*offset, "empty name")) ?,
                    Some(length) => match std::str::from_utf8(&slice[..length]) {
                        Ok(string) => { *offset += 8; Some(string) },
                        Err(_) => Err(MiError::bad_value(*offset, "non-utf8 name"))?,
                    },
                },
                None => Err(MiError::end_of_buffer(*offset, 8, from.len()))?,
            },
        };

        let value_start = *offset;
        let payload = from.get(value_start - element_start ..).unwrap();

        let value = match mi_type {
            MiType::None => MiValue::None,
            MiType::Object => MiObjectView::read_from(payload, offset).map(MiValue::Object)?,
            MiType::Array => MiArrayErased::read_from(payload, offset).map(MiValue::Array)?,
            MiType::Map => MiMapErased::read_from(payload, offset).map(MiValue::Map)?,
            MiType::Int32 => i32::read_value(payload, offset).map(MiValue::Int32)?,
            MiType::Int64 => i64::read_value(payload, offset).map(MiValue::Int64)?,
            MiType::Single => f32::read_value(payload, offset).map(MiValue::Single)?,
            MiType::Double => f64::read_value(payload, offset).map(MiValue::Double)?,
            MiType::Bool => bool::read_value(payload, offset).map(MiValue::Bool)?,
            MiType::String => <&'a str>::read_value(payload, offset).map(MiValue::String)?,
            MiType::Binary => <&'a [u8]>::read_value(payload, offset).map(MiValue::Binary)?,
        };

        Ok(Self { offset: element_start, name, value })
    }

    /// Wrapper for [`Self::read_from`] which advances input slice after itself.
    fn read_shift<'f: 'a>(from: &mut &'f [u8], offset: &mut usize) -> Result<Self, MiError> {
        let start = *offset;
        Self::read_from(from, offset).inspect(|_| *from = from.get(*offset - start..).unwrap())
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
    initial: (usize, &'a [u8]),
    current: Cell<(usize, &'a [u8])>,
}

impl<'a> MiObjectView<'a> {
    pub(crate) fn new<'s: 'a>(count: usize, offset: usize, source: &'s [u8]) -> Self {
        Self { count, initial: (offset, source), current: Cell::new((offset, source)) }
    }

    /// Tries to deserialize an object `from` a slice.
    pub fn read_from<'s: 'a>(from: &'s [u8], offset: &mut usize) -> Result<Self, MiError> {
        let header_start = *offset;
        let MiObjectHeader { size, count } = MiObjectHeader::read_from(from, offset)?;

        let payload_start = *offset;
        let payload_end = payload_start + size as usize;

        debug_assert_eq!(payload_start - header_start, MiObjectHeader::SERIAL_SIZE);

        match from.get(payload_start .. payload_end) {
            Some(slice) => {
                *offset += size as usize;
                Ok(Self::new(count as usize, payload_start, slice))
            },
            None => Err(MiError::end_of_buffer(payload_start, payload_end, from.len())),
        }
    }

    /// Checks if the object is empty, i.e. has no elements.
    pub fn is_empty(&self) -> bool {
        self.count == 0
    }

    /// Retrieves number of elements in the object.
    pub fn len(&self) -> usize {
        self.count
    }

    /// Retrieves number of bytes in the initial slice.
    pub fn get_init_len(&self) -> usize {
        self.initial.1.len()
    }

    /// Checks if object iteration has reached end.
    pub fn at_end(&self) -> bool {
        self.current.get().1.is_empty()
    }

    /// Resets object iteration to its initial state.
    pub fn reset(&self) {
        self.current.set(self.initial)
    }

    /// Tries to deserialize next element in the object.
    pub fn next_element<'f: 'a>(&'f self) -> MiReadResult<MiElement> {
        if self.at_end() { return MiReadResult::None; }
        let implement = || -> Result<MiElement, MiError> {
            let (mut item_start, mut item_slice) = self.current.get();
            let element = MiElement::read_shift(&mut item_slice, &mut item_start)?;
            self.current.set((item_start, item_slice));
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
    pub const SERIAL_SIZE: usize = 6;

    /// Tries to deserialize a header `from` a slice.
    pub fn read_from(from: &[u8], offset: &mut usize) -> Result<Self, MiError> {
        let size = match from.get(.. 4) {
            Some(slice) => { *offset += 4; u32::from_be_bytes(slice.try_into().unwrap()) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len()))?,
        };
        let count = match from.get(4 .. 6) {
            Some(slice) => { *offset += 2; u16::from_be_bytes(slice.try_into().unwrap()) },
            None => Err(MiError::end_of_buffer(*offset, 6, from.len()))?,
        };
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
    initial: (usize, &'a [u8]),
    current: Cell<(usize, &'a [u8])>,
    _marker: PhantomData<&'a Item>,
}

impl<'a, Item> MiArrayView<'a, Item>
where
    Item: SerialValue<'a>,
{
    pub(crate) fn new<'s: 'a>(count: usize, offset: usize, source: &'s [u8]) -> Self {
        Self { count, initial: (offset, source), current: Cell::new((offset, source)), _marker: PhantomData }
    }

    /// Tries to materialize a type-`erased` array into a typed array view.
    pub fn view<'s: 'a>(erased: MiArrayErased<'a>) -> Result<Self, MiError> {
        match erased.item_type {
            t if t == Item::MI_TYPE => Ok(Self::new(erased.count, erased.init_offset, erased.init_slice)),
            _ => Err(MiError::value_type(Item::MI_TYPE, erased.item_type)),
        }
    }

    /// Tries to deserialize an array `from` a slice.
    pub fn read_from<'s: 'a>(from: &'s [u8], offset: &mut usize) -> Result<Self, MiError> {
        MiArrayErased::read_from(from, offset).and_then(Self::view)
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
        self.current.get().1.is_empty()
    }

    /// Resets array iteration to its initial state.
    pub fn reset(&self) {
        self.current.set(self.initial)
    }

    /// Tries to parse next value in the array.
    pub fn next_value<'f: 'a>(&'f self) -> MiReadResult<Item> {
        if self.at_end() { return MiReadResult::None; }
        let implement = || -> Result<Item, MiError> {
            let (mut item_start, mut item_slice) = self.current.get();
            let key = Item::read_shift(&mut item_slice, &mut item_start)?;
            self.current.set((item_start, item_slice));
            Ok(key)
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
    /// Initial serialization offset.
    pub init_offset: usize,
    /// Initial slice over the payload.
    pub init_slice: &'a [u8],
}

impl<'a> MiArrayErased<'a> {
    /// Tries to deserialize an array `from` a slice.
    pub fn read_from<'s: 'a>(from: &'s [u8], offset: &mut usize) -> Result<Self, MiError> {
        let header_start = *offset;
        let MiArrayHeader { size, count, item_type } = MiArrayHeader::read_from(from, offset)?;

        let payload_start = *offset;
        let payload_end = payload_start + size as usize;

        debug_assert_eq!(payload_start - header_start, MiArrayHeader::SERIAL_SIZE);

        match from.get(payload_start .. payload_end) {
            Some(init_slice) => {
                *offset += size as usize;
                let init_offset = payload_start;
                Ok(Self { count: count as usize, item_type, init_offset, init_slice })
            },
            None => Err(MiError::end_of_buffer(payload_start, payload_end, from.len())),
        }
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
    pub const SERIAL_SIZE: usize = 7;

    /// Tries to deserialize a header `from` a slice.
    pub fn read_from(from: &[u8], offset: &mut usize) -> Result<Self, MiError> {
        let size = match from.get(.. 4) {
            Some(slice) => { *offset += 4; u32::from_be_bytes(slice.try_into().unwrap()) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len()))?,
        };
        let count = match from.get(4 .. 6) {
            Some(slice) => { *offset += 2; u16::from_be_bytes(slice.try_into().unwrap()) },
            None => Err(MiError::end_of_buffer(*offset, 6, from.len()))?,
        };
        let mi_type = match from.get(6) {
            Some(&tag) => { *offset += 1; MiType::from_tag(tag) },
            None => Err(MiError::end_of_buffer(*offset, 7, from.len()))?,
        };

        let item_type = mi_type.ok_or(MiError::bad_value(*offset - 1, "invalid type"))?;
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
    initial: (usize, &'a [u8]),
    current: Cell<(usize, &'a [u8])>,
    _marker: PhantomData<&'a (Key, Value)>,
}

impl<'a, Key, Value> MiMapView<'a, Key, Value>
where
    Key: SerialValue<'a>,
    Value: SerialValue<'a>,
{
    pub(crate) fn new<'s: 'a>(count: usize, offset: usize, source: &'s [u8]) -> Self {
        Self { count, initial: (offset, source), current: Cell::new((offset, source)), _marker: PhantomData }
    }

    /// Tries to materialize an `erased` map into a typed map view.
    pub fn view<'s: 'a>(erased: MiMapErased<'a>) -> Result<Self, MiError> {
        match (erased.key_type, erased.value_type) {
            (key, value) if (key, value) == (Key::MI_TYPE, Value::MI_TYPE) =>
                Ok(Self::new(erased.count, erased.init_offset, erased.init_slice)),
            (_, value) if value == Value::MI_TYPE =>
                Err(MiError::key_type(Key::MI_TYPE, erased.key_type)),
            _ =>  // this handles "wrong key" and "both wrong" cases
                Err(MiError::value_type(Value::MI_TYPE, erased.value_type)),
        }
    }

    /// Tries to deserialize a map `from` a slice.
    pub fn read_from<'s: 'a>(from: &'s [u8], offset: &mut usize) -> Result<Self, MiError> {
        MiMapErased::read_from(from, offset).and_then(Self::view)
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
        self.current.get().1.is_empty()
    }

    /// Resets map iteration to its initial state.
    pub fn reset(&self) {
        self.current.set(self.initial)
    }

    /// Tries to parse next pair in the map.
    pub fn next_pair<'f: 'a>(&'f self) -> MiReadResult<(Key, Value)> {
        if self.at_end() { return MiReadResult::None; }
        let implement = || -> Result<(Key, Value), MiError> {
            let (mut item_start, mut item_slice) = self.current.get();
            let key = Key::read_shift(&mut item_slice, &mut item_start)?;
            let value = Value::read_shift(&mut item_slice, &mut item_start)?;
            self.current.set((item_start, item_slice));
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
    /// Initial serialization offset.
    pub init_offset: usize,
    /// Initial slice over the payload.
    pub init_slice: &'a [u8],
}

impl<'a> MiMapErased<'a> {
    /// Tries to deserialize a map `from` a slice.
    pub fn read_from<'s: 'a>(from: &'s [u8], offset: &mut usize) -> Result<Self, MiError> {
        let header_start = *offset;
        let MiMapHeader { size, count, key_type, value_type } = MiMapHeader::read_from(from, offset)?;

        let payload_start = *offset;
        let payload_end = payload_start + size as usize;

        debug_assert_eq!(payload_start - header_start, MiMapHeader::SERIAL_SIZE);

        match from.get(payload_start .. payload_end) {
            Some(init_slice) => {
                *offset += size as usize;
                let init_offset = payload_start;
                Ok(Self { count: count as usize, key_type, value_type, init_offset, init_slice })
            }
            None => Err(MiError::end_of_buffer(payload_start, payload_end, from.len())),
        }
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
    pub const SERIAL_SIZE: usize = 8;

    /// Tries to deserialize a header `from` a slice.
    pub fn read_from(from: &[u8], offset: &mut usize) -> Result<Self, MiError> {
        let size = match from.get(.. 4) {
            Some(slice) => { *offset += 4; u32::from_be_bytes(slice.try_into().unwrap()) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len()))?,
        };
        let count = match from.get(4 .. 6) {
            Some(slice) => { *offset += 2; u16::from_be_bytes(slice.try_into().unwrap()) },
            None => Err(MiError::end_of_buffer(*offset, 6, from.len()))?,
        };
        let key_type = match from.get(6) {
            Some(&tag) => { *offset += 1; MiType::from_tag(tag) },
            None => Err(MiError::end_of_buffer(*offset, 7, from.len()))?,
        };
        let value_type = match from.get(7) {
            Some(&tag) => { *offset += 1; MiType::from_tag(tag) },
            None => Err(MiError::end_of_buffer(*offset, 8, from.len()))?,
        };

        let key_type = key_type.ok_or(MiError::bad_value(*offset - 2, "invalid key type"))?;
        let value_type = value_type.ok_or(MiError::bad_value(*offset - 1, "invalid value type"))?;

        Ok(Self { size, count, key_type, value_type })
    }
}


#[cfg(test)]
mod tests {
    use crate::read::{MiArrayView, MiReadResult};

    #[test]
    fn test_array_lifetime() {
        let array = MiArrayView::<i32>::new(0, 0, &[]);
        assert_eq!(array.len(), 0);
        while let MiReadResult::Ok(value) = array.next_value() {
            println!("value => {}", value);
        }
    }
}
