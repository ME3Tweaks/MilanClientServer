//! Reference implementation for MISON deserialization.

use std::marker::PhantomData;
use thiserror::Error;

use crate::MiType;
use crate::io::{LocalSerialInput, SerialInput};


/// Errors produced by *document* deserialization.
#[derive(Clone, Debug, Error)]
pub enum ReadError {
    /// Invalid name table entry.
    #[error("invalid name table entry #{i}: {reason}")]
    BadNameEntry { i: u16, reason: &'static str },
    /// Invalid name reference.
    #[error("invalid name offset {offset}: {reason}")]
    BadNameRef { offset: u16, reason: &'static str },
    /// Invalid value serialization.
    #[error("invalid value serialization of type {r#type}: {reason}")]
    BadValue { r#type: MiType, reason: &'static str },
    /// Unexpected end of buffer.
    #[error("unexpected end of buffer at {offset}, read {found} of {expected} byte(s)")]
    EndOfBuffer { expected: usize, found: usize, offset: usize },
    /// Uncategorized error.
    #[error("{0}")]
    Other(&'static str),
}


/// Provides generic value deserialization.
pub trait ReadValue<'a>: Sized {
    /// Associated type marker.
    const MI_TYPE: MiType;

    /// Tries to deserialize a value of this type from `input`.
    fn read_value(decoder: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError>;
}

impl<'a> ReadValue<'a> for () {
    const MI_TYPE: MiType = MiType::None;
    fn read_value(_: &'a MiDecoder<'a>, _: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        Ok(())
    }
}

impl<'a> ReadValue<'a> for MiObject<'a> {
    const MI_TYPE: MiType = MiType::Object;
    fn read_value(decoder: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        Self::decode(decoder, input)
    }
}

impl<'a> ReadValue<'a> for MiArrayView<'a> {
    const MI_TYPE: MiType = MiType::Array;
    fn read_value(decoder: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        Self::decode(decoder, input)
    }
}

impl<'a> ReadValue<'a> for MiMapView<'a> {
    const MI_TYPE: MiType = MiType::Map;
    fn read_value(decoder: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        Self::decode(decoder, input)
    }
}

impl<'a> ReadValue<'a> for i32 {
    const MI_TYPE: MiType = MiType::Int32;
    fn read_value(_: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        input.get_array::<4>().map(Self::from_be_bytes)
    }
}

impl<'a> ReadValue<'a> for i64 {
    const MI_TYPE: MiType = MiType::Int64;
    fn read_value(_: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        input.get_array::<8>().map(Self::from_be_bytes)
    }
}

impl<'a> ReadValue<'a> for f32 {
    const MI_TYPE: MiType = MiType::Single;
    fn read_value(_: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        input.get_array::<4>().map(Self::from_be_bytes)
    }
}

impl<'a> ReadValue<'a> for f64 {
    const MI_TYPE: MiType = MiType::Double;
    fn read_value(_: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        input.get_array::<8>().map(Self::from_be_bytes)
    }
}

impl<'a> ReadValue<'a> for bool {
    const MI_TYPE: MiType = MiType::Bool;
    fn read_value(_: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        match input.get_byte()? {
            value @ (0_u8 | 1_u8) => Ok(value != 0_u8),
            _ => Err(ReadError::BadValue { r#type: Self::MI_TYPE, reason: "value > 1" }),
        }
    }
}

impl<'a> ReadValue<'a> for &'a str {
    const MI_TYPE: MiType = MiType::String;

    fn read_value(_: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        let length = input.get_array::<4>().map(u32::from_be_bytes)?;
        let terminated = input.get_slice(length as usize)?;

        let slice = match terminated.split_last() {
            Some((&0_u8, slice)) => slice,
            Some(..) => Err(ReadError::BadValue { r#type: Self::MI_TYPE, reason: "not terminated" })?,
            None => Err(ReadError::BadValue { r#type: Self::MI_TYPE, reason: "zero length" })?,
        };

        std::str::from_utf8(slice).map_err(|_| ReadError::BadValue { r#type: Self::MI_TYPE, reason: "not utf-8" })
    }
}

impl<'a> ReadValue<'a> for &'a [u8] {
    const MI_TYPE: MiType = MiType::Bool;
    fn read_value(_: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        let length = input.get_array::<4>().map(u32::from_be_bytes)?;
        input.get_slice(length as usize)
    }
}


/// Semi-deserialized value.
#[derive(Clone)]
pub enum MiValue<'a> {
    None,
    Object(MiObject<'a>),
    Array(MiArrayView<'a>),
    Map(MiMapView<'a>),
    Int32(i32),
    Int64(i64),
    Single(f32),
    Double(f64),
    Bool(bool),
    String(&'a str),
    Binary(&'a [u8]),
}

impl<'a> MiValue<'a> {
    /// Map this value variant to its type marker.
    pub fn get_type(&self) -> MiType {
        match self {
            MiValue::None => MiType::None,
            MiValue::Object(_) => MiType::Object,
            MiValue::Array(_) => MiType::Array,
            MiValue::Map(_) => MiType::Map,
            MiValue::Int32(_) => MiType::Int32,
            MiValue::Int64(_) => MiType::Int64,
            MiValue::Single(_) => MiType::Single,
            MiValue::Double(_) => MiType::Double,
            MiValue::Bool(_) => MiType::Bool,
            MiValue::String(_) => MiType::String,
            MiValue::Binary(_) => MiType::Binary,
        }
    }
}


/// Encapsulates deserialization of a document.
pub struct MiDecoder<'a> {
    names: &'a [u8],
    element: &'a [u8],
}

impl<'a> MiDecoder<'a> {
    /// Starts decoding a complete document payload.
    pub fn decode(slice: &'a impl AsRef<[u8]>) -> Result<Self, ReadError> {
        Self::decode_internal(slice.as_ref())
    }

    fn decode_internal(slice: &'a [u8]) -> Result<Self, ReadError> {
        let input = LocalSerialInput::new(slice);

        // Read and validate the name table.
        let names = {
            let size = u16::from_be_bytes(input.get_array::<2>()?);
            let count = u16::from_be_bytes(input.get_array::<2>()?);

            for i in 0 .. count {
                let length = input.get_byte()? as usize;
                let slice = input.get_slice(length.saturating_sub(1))?;
                match std::str::from_utf8(slice) {
                    Ok(s) if s.is_ascii() => {},
                    _ => Err(ReadError::BadNameEntry { i, reason: "not ascii" })?,
                };

                let terminator = input.get_byte()?;
                if terminator != 0_u8 {
                    Err(ReadError::BadNameEntry { i, reason: "not null-terminated" })?;
                }
            }

            if input.pos() != (size + 4) as usize {
                Err(ReadError::Other("mismatched end of name table"))?;
            }

            input.processed()
        };

        Ok(MiDecoder { names, element: input.remaining() })
    }

    /// Attempts to decode the root element of this document.
    pub fn root(&self) -> Result<MiElement<'_>, ReadError> {
        MiElement::decode(self, &LocalSerialInput::new(self.element))
    }

    /// Checks if a name `offset` is plausible.
    fn is_valid_offset(&self, offset: u16) -> bool {
        offset >= 4 && offset < (self.names.len() as u16)
    }

    /// Attempts to read an element name by its `offset`.
    fn get_name(&self, offset: u16) -> Result<Option<&str>, ReadError> {
        if offset == u16::MAX { return Ok(None); }
        let bad_name = |reason: &'static str| ReadError::BadNameRef { offset, reason };

        let (min, max) = (4_u16, self.names.len() as u16 - 2);
        if offset < min || offset > max { Err(bad_name("out of bounds"))?; }

        // SAFETY: We have just checked offset to be within bounds...
        let length = unsafe { *self.names.get_unchecked(offset as usize) } as usize;

        let start = offset as usize + 1;
        match self.names.get(start .. start + length) {
            Some(slice) if !slice.is_empty() => {
                let (&null, chars) = slice.split_last().unwrap();
                if null != 0_u8 { Err(bad_name("not null-terminated"))?; }
                match std::str::from_utf8(chars) {
                    Ok(string) => Ok(Some(string)),
                    Err(_) => Err(bad_name("not ascii-encoded"))?,
                }
            },
            _ => Err(bad_name("invalid length"))?,
        }
    }
}


/// Semi-deserialized representation of an element.
pub struct MiElement<'a> {
    pub name: Option<&'a str>,
    pub value: MiValue<'a>,
}

impl<'a> MiElement<'a> {
    fn decode(decoder: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        let r#type = input.get_type()?;
        let name = decoder.get_name(u16::from_be_bytes(input.get_array::<2>()?))?;

        let value = match r#type {
            MiType::None => MiValue::None,
            MiType::Object => MiObject::<'a>::read_value(decoder, input).map(MiValue::Object)?,
            MiType::Array => MiArrayView::<'a>::read_value(decoder, input).map(MiValue::Array)?,
            MiType::Map => MiMapView::<'a>::read_value(decoder, input).map(MiValue::Map)?,
            MiType::Int32 => i32::read_value(decoder, input).map(MiValue::Int32)?,
            MiType::Int64 => i64::read_value(decoder, input).map(MiValue::Int64)?,
            MiType::Single => f32::read_value(decoder, input).map(MiValue::Single)?,
            MiType::Double => f64::read_value(decoder, input).map(MiValue::Double)?,
            MiType::Bool => bool::read_value(decoder, input).map(MiValue::Bool)?,
            MiType::String => <&'a str>::read_value(decoder, input).map(MiValue::String)?,
            MiType::Binary => <&'a [u8]>::read_value(decoder, input).map(MiValue::Binary)?,
        };

        Ok(Self { name, value })
    }
}


/// Access to an object value.
///
/// Note that object value is unlike array or map: it has no inherent item type,
/// since object elements serialize their own types, which are allowed to be different.
/// Thus, unlike with arrays and maps, there is no `MiObjectView` type: it is unnecessary.
#[derive(Clone)]
pub struct MiObject<'a> {
    /// Number of elements that *should be* serialized in this object.
    pub count: usize,

    payload: &'a [u8],
    decoder: &'a MiDecoder<'a>,
}

impl<'a> MiObject<'a> {
    fn decode(decoder: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        let size = u32::from_be_bytes(input.get_array::<4>()?) as usize;
        let count = u16::from_be_bytes(input.get_array::<2>()?) as usize;
        let payload = input.get_slice(size)?;
        Ok(Self { count, payload, decoder })
    }
}

impl<'a> IntoIterator for &MiObject<'a> {
    type Item = Result<MiElement<'a>, ReadError>;
    type IntoIter = MiObjectIter<'a>;
    fn into_iter(self) -> Self::IntoIter {
        MiObjectIter::<'a>::new(self)
    }
}


/// Deserializing iterator over [`MiObject`] elements.
pub struct MiObjectIter<'a> {
    index: usize,
    count: usize,
    input: LocalSerialInput<'a>,
    decoder: &'a MiDecoder<'a>,
}

impl<'a> MiObjectIter<'a> {
    fn new(object: &MiObject<'a>) -> Self {
        Self {
            index: 0,
            count: object.count,
            input: LocalSerialInput::new(object.payload),
            decoder: object.decoder,
        }
    }

    /// Resets iteration to the beginning.
    pub fn reset(&mut self) {
        self.index = 0;
        self.input.reset();
    }
}

impl<'a> Iterator for MiObjectIter<'a> {
    type Item = Result<MiElement<'a>, ReadError>;
    fn next(&mut self) -> Option<Self::Item> {
        if self.index == self.count { return None; }
        match MiElement::decode(self.decoder, &self.input) {
            Ok(element) => { self.index += 1; Some(Ok(element)) },
            Err(err) => Some(Err(err)),
        }
    }
}


/// Strongly-typed access to an array value.
///
/// This type can be constructed by calling [`MiArrayView::to_typed`],
/// or via [`TryFrom`] / [`TryInto`] traits, which internally call it.
pub struct MiArray<'a, Value> {
    /// Number of items that *should be* serialized in this array.
    pub count: usize,

    payload: &'a [u8],
    decoder: &'a MiDecoder<'a>,
    _marker: PhantomData<&'a [Value]>,
}

impl<'a, Value: ReadValue<'a>> TryFrom<&MiArrayView<'a>> for MiArray<'a, Value> {
    type Error = ();
    fn try_from(value: &MiArrayView<'a>) -> Result<Self, Self::Error> {
        value.to_typed().ok_or(())
    }
}

impl<'a, Value: ReadValue<'a>> IntoIterator for &MiArray<'a, Value> {
    type Item = Result<Value, ReadError>;
    type IntoIter = MiArrayIter<'a, Value>;
    fn into_iter(self) -> Self::IntoIter {
        MiArrayIter::<'a, Value>::new(self)
    }
}


/// Deserializing iterator over [`MiArray`] items.
pub struct MiArrayIter<'a, Value> {
    index: usize,
    count: usize,
    input: LocalSerialInput<'a>,
    decoder: &'a MiDecoder<'a>,
    _marker: PhantomData<&'a [Value]>,
}

impl<'a, Value> MiArrayIter<'a, Value> {
    fn new(array: &MiArray<'a, Value>) -> Self {
        Self {
            index: 0,
            count: array.count,
            input: LocalSerialInput::new(array.payload),
            decoder: array.decoder,
            _marker: PhantomData,
        }
    }

    /// Resets iteration to the beginning.
    pub fn reset(&mut self) {
        self.index = 0;
        self.input.reset();
    }
}

impl<'a, Value: ReadValue<'a>> Iterator for MiArrayIter<'a, Value> {
    type Item = Result<Value, ReadError>;
    fn next(&mut self) -> Option<Self::Item> {
        if self.index == self.count { return None; }
        match Value::read_value(self.decoder, &self.input) {
            Ok(value) => { self.index += 1; Some(Ok(value)) },
            Err(err) => Some(Err(err)),
        }
    }
}


/// Untyped view over an array value which can construct [`MiArray`].
#[derive(Clone)]
pub struct MiArrayView<'a> {
    /// Item value type.
    pub value_type: MiType,
    /// Number of items that *should be* serialized in this array.
    pub count: usize,

    payload: &'a [u8],
    decoder: &'a MiDecoder<'a>,
}

impl<'a> MiArrayView<'a> {
    fn decode(decoder: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        let size = u32::from_be_bytes(input.get_array::<4>()?) as usize;
        let count = u16::from_be_bytes(input.get_array::<2>()?) as usize;
        let value_type = input.get_type()?;
        let payload = input.get_slice(size)?;
        Ok(Self { value_type, count, payload, decoder })
    }

    /// Constructs a strongly-typed array accessor, checking the type.
    pub fn to_typed<Value: ReadValue<'a>>(&self) -> Option<MiArray<'a, Value>> {
        if self.value_type != Value::MI_TYPE { return None; }
        Some(MiArray {
            count: self.count,
            payload: self.payload,
            decoder: self.decoder,
            _marker: PhantomData,
        })
    }
}


/// Strongly-typed access to a map value.
///
/// This type can be constructed by calling [`MiMapView::to_typed`],
/// or via [`TryFrom`] / [`TryInto`] traits, which internally call it.
pub struct MiMap<'a, Key, Value> {
    /// Number of pairs that *should be* serialized in this array.
    pub count: usize,

    payload: &'a [u8],
    decoder: &'a MiDecoder<'a>,
    _marker: PhantomData<&'a [(Key, Value)]>,
}

impl<'a, Key: ReadValue<'a>, Value: ReadValue<'a>> TryFrom<&MiMapView<'a>> for MiMap<'a, Key, Value> {
    type Error = ();
    fn try_from(value: &MiMapView<'a>) -> Result<Self, Self::Error> {
        value.to_typed().ok_or(())
    }
}

impl<'a, Key: ReadValue<'a>, Value: ReadValue<'a>> IntoIterator for &MiMap<'a, Key, Value> {
    type Item = Result<(Key, Value), ReadError>;
    type IntoIter = MiMapIter<'a, Key, Value>;
    fn into_iter(self) -> Self::IntoIter {
        MiMapIter::<'a, Key, Value>::new(self)
    }
}

/// Deserializing iterator over [`MiMap`] key-value pairs.
pub struct MiMapIter<'a, Key, Value> {
    index: usize,
    count: usize,
    input: LocalSerialInput<'a>,
    decoder: &'a MiDecoder<'a>,
    _marker: PhantomData<&'a [(Key, Value)]>,
}

impl<'a, Key, Value> MiMapIter<'a, Key, Value> {
    fn new(map: &MiMap<'a, Key, Value>) -> Self {
        Self {
            index: 0,
            count: map.count,
            input: LocalSerialInput::new(map.payload),
            decoder: map.decoder,
            _marker: PhantomData,
        }
    }

    /// Resets iteration to the beginning.
    pub fn reset(&mut self) {
        self.index = 0;
        self.input.reset();
    }
}

impl<'a, Key: ReadValue<'a>, Value: ReadValue<'a>> Iterator for MiMapIter<'a, Key, Value> {
    type Item = Result<(Key, Value), ReadError>;
    fn next(&mut self) -> Option<Self::Item> {
        if self.index == self.count { return None; }

        let key = match Key::read_value(self.decoder, &self.input) {
            Ok(key) => key,
            Err(err) => return Some(Err(err)),
        };
        let value = match Value::read_value(self.decoder, &self.input) {
            Ok(value) => value,
            Err(err) => return Some(Err(err)),
        };

        self.index += 1;
        Some(Ok((key, value)))
    }
}


/// Untyped view over a map value which can construct [`MiMap`].
#[derive(Clone)]
pub struct MiMapView<'a> {
    /// Pair key type.
    pub key_type: MiType,
    /// Pair value type.
    pub value_type: MiType,
    /// Number of items that *should be* serialized in this array.
    pub count: usize,

    payload: &'a [u8],
    decoder: &'a MiDecoder<'a>,
}

impl<'a> MiMapView<'a> {
    fn decode(decoder: &'a MiDecoder<'a>, input: &impl SerialInput<'a>) -> Result<Self, ReadError> {
        let size = u32::from_be_bytes(input.get_array::<4>()?) as usize;
        let count = u16::from_be_bytes(input.get_array::<2>()?) as usize;
        let key_type = input.get_type()?;
        let value_type = input.get_type()?;
        let payload = input.get_slice(size)?;
        Ok(Self { key_type, value_type, count, payload, decoder })
    }

    /// Constructs a strongly-typed key accessor, checking key / value types.
    pub fn to_typed<Key, Value>(&self) -> Option<MiMap<'a, Key, Value>>
    where
        Key: ReadValue<'a>,
        Value: ReadValue<'a>,
    {
        if self.key_type != Key::MI_TYPE { return None; }
        if self.value_type != Value::MI_TYPE { return None; }
        Some(MiMap {
            count: self.count,
            payload: self.payload,
            decoder: self.decoder,
            _marker: PhantomData,
        })
    }
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_array_read() {
        let bytes = crate::write::tests::parse_hex(
            "00 07 00 01 06 41 52 52 41 59 00 02 00 04 00 00 00 0C 00 03 04 00 00 00 02 00 00 00 04 00 00 00 08"
        );

        let decoder = MiDecoder::decode(&bytes).unwrap();
        let root = decoder.root().unwrap();
        assert!(matches!(root.name, Some("ARRAY")));

        let MiValue::Array(array) = root.value else { panic!(); };
        assert_eq!(array.value_type, MiType::Int32);
        assert_eq!(array.count, 3);

        let array = array.to_typed::<i32>().unwrap();
        assert_eq!(array.count, 3);

        let mut iterator = MiArrayIter::new(&array);
        assert!(matches!(iterator.next(), Some(Ok(2_i32))));
        assert!(matches!(iterator.next(), Some(Ok(4_i32))));
        assert!(matches!(iterator.next(), Some(Ok(8_i32))));
        assert!(matches!(iterator.next(), None));

        iterator.reset();
        assert!(matches!(iterator.next(), Some(Ok(2_i32))));
        assert!(matches!(iterator.next(), Some(Ok(4_i32))));
        assert!(matches!(iterator.next(), Some(Ok(8_i32))));
        assert!(matches!(iterator.next(), None));
    }

    #[test]
    fn test_array_str_lifetime() {
        #[inline(never)]
        fn print_strings(array: &MiArray<&str>) {
            for string in array {
                dbg!(string).unwrap();
            }
        }
    }

    #[test]
    fn test_object_lifetime() {
        #[inline(never)]
        fn print_elements(object: &MiObject) {
            for elem in object {
                let MiElement { name, value } = elem.unwrap();
                println!("{} => {}", name.unwrap_or("<anonymous>"), value.get_type())
            }
        }
    }
}
