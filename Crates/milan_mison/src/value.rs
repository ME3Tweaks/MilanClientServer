//! Deserializable value representation.

use std::str;

use crate::MiType;
use crate::read::{MiArrayErased, MiArrayHeader, MiError, MiMapErased, MiMapHeader, MiObjectHeader, MiObjectView};


/// Shallowly deserialized MISON value.
#[derive(Clone, Debug)]
pub enum MiValue<'a> {
    None,
    Object(MiObjectView<'a>),
    Array(MiArrayErased<'a>),
    Map(MiMapErased<'a>),
    Int32(i32),
    Int64(i64),
    Single(f32),
    Double(f64),
    Bool(bool),
    String(&'a str),
    Binary(&'a [u8]),
}

impl<'a> MiValue<'a> {
    /// Maps value to its type marker.
    pub fn get_type(&self) -> MiType {
        match self {
            MiValue::None => MiType::None,
            MiValue::Array(_) => MiType::Array,
            MiValue::Object(_) => MiType::Object,
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

    /// Calculates the number of bytes this value takes in serialized form.
    pub fn get_serial_size(&self) -> usize {
        match self {
            MiValue::None => 0,
            MiValue::Array(array) =>
                array.init_slice.len() + MiArrayHeader::SERIAL_SIZE,
            MiValue::Object(object) =>
                object.get_init_len() + MiObjectHeader::SERIAL_SIZE,
            MiValue::Map(map) =>
                map.init_slice.len() + MiMapHeader::SERIAL_SIZE,
            MiValue::Int32(_) => 4,
            MiValue::Int64(_) => 8,
            MiValue::Single(_) => 4,
            MiValue::Double(_) => 8,
            MiValue::Bool(_) => 1,
            MiValue::String(string) => string.as_bytes().len() + 5,
            MiValue::Binary(blob) => blob.len() + 4,
        }
    }
}

impl<'a> From<&MiValue<'a>> for MiType {
    fn from(value: &MiValue<'a>) -> Self {
        value.get_type()
    }
}


/// Provides generic value deserialization.
pub trait SerialValue<'a>: Sized {
    /// Associated type tag.
    const MI_TYPE: MiType;

    /// Tries to deserialize a value of this type `from` a slice.
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError>;

    /// Wrapper for [`Self::read_value`] which advances input slice after itself.
    fn read_shift(from: &mut &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        let start = *offset;
        Self::read_value(from, offset).inspect(|_| *from = from.get(*offset - start..).unwrap())
    }
}

impl<'a> SerialValue<'a> for () {
    const MI_TYPE: MiType = MiType::None;
    fn read_value(_: &'a [u8], _: &mut usize) -> Result<Self, MiError> {
        Ok(())
    }
}

impl<'a> SerialValue<'a> for MiArrayErased<'a> {
    const MI_TYPE: MiType = MiType::Array;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        MiArrayErased::read_from(from, offset)
    }
}

impl<'a> SerialValue<'a> for MiObjectView<'a> {
    const MI_TYPE: MiType = MiType::Object;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        MiObjectView::read_from(from, offset)
    }
}

impl<'a> SerialValue<'a> for MiMapErased<'a> {
    const MI_TYPE: MiType = MiType::Map;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        MiMapErased::read_from(from, offset)
    }
}

impl<'a> SerialValue<'a> for i32 {
    const MI_TYPE: MiType = MiType::Int32;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        match from.get(.. 4) {
            Some(slice) => { *offset += 4; Ok(Self::from_be_bytes(slice.try_into().unwrap())) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len())),
        }
    }
}

impl<'a> SerialValue<'a> for i64 {
    const MI_TYPE: MiType = MiType::Int64;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        match from.get(.. 8) {
            Some(slice) => { *offset += 8; Ok(Self::from_be_bytes(slice.try_into().unwrap())) },
            None => Err(MiError::end_of_buffer(*offset, 8, from.len())),
        }
    }
}

impl<'a> SerialValue<'a> for f32 {
    const MI_TYPE: MiType = MiType::Single;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        match from.get(.. 4) {
            Some(slice) => { *offset += 4; Ok(Self::from_be_bytes(slice.try_into().unwrap())) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len())),
        }
    }
}

impl<'a> SerialValue<'a> for f64 {
    const MI_TYPE: MiType = MiType::Double;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
            match from.get(.. 8) {
            Some(slice) => { *offset += 8; Ok(Self::from_be_bytes(slice.try_into().unwrap())) },
            None => Err(MiError::end_of_buffer(*offset, 8, from.len())),
        }
    }
}

impl<'a> SerialValue<'a> for bool {
    const MI_TYPE: MiType = MiType::Bool;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        match from.first() {
            Some(&0_u8) => { *offset += 1; Ok(false) },
            Some(&1_u8) => { *offset += 1; Ok(true) },
            Some(_) => { Err(MiError::bad_value(*offset, "bool > 1")) },
            None => { Err(MiError::end_of_buffer(*offset, 1, 0)) },
        }
    }
}

impl<'a> SerialValue<'a> for &'a str {
    const MI_TYPE: MiType = MiType::String;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        let length = match from.get(.. 4) {
            Some(slice) => { *offset += 4; u32::from_be_bytes(slice.try_into().unwrap()) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len()))?,
        };

        let terminated = match from.get(4 .. 4 + length as usize) {
            Some(terminated) => { *offset += length as usize; terminated },
            None => Err(MiError::end_of_buffer(*offset, 4 + length as usize, from.len()))?,
        };

        let slice = match terminated.split_last() {
            Some((&0_u8, slice)) => slice,
            Some(..) => Err(MiError::bad_value(*offset - 1, "string not terminated"))?,
            None => Err(MiError::bad_value(*offset - length as usize, "null string size"))?,
        };

        str::from_utf8(slice).map_err(move |_| MiError::bad_value(*offset, "not utf-8"))
    }
}

impl<'a> SerialValue<'a> for &'a [u8] {
    const MI_TYPE: MiType = MiType::Binary;
    fn read_value(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        let length = match from.get(.. 4) {
            Some(slice) => { *offset += 4; u32::from_be_bytes(slice.try_into().unwrap()) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len()))?,
        };

        match from.get(4 .. 4 + length as usize) {
            Some(slice) => { *offset += length as usize; Ok(slice) },
            None => Err(MiError::end_of_buffer(*offset, 4 + length as usize, from.len()))?,
        }
    }
}


#[cfg(test)]
mod tests {
    use super::{MiValue, SerialValue};

    #[test]
    fn check_string_lifetime() {
        let source: &[u8] = b"\x00\x00\x00\x0DHello there!\x00";

        fn foo(s: &[u8]) -> &str {
            let mut offset = 0_usize;
            SerialValue::read_value(s, &mut offset).unwrap()
        }

        assert_eq!(foo(source).as_bytes(), &source[4..16]);
    }

    #[test]
    fn log_value_size() {
        println!("size of MiValue => {}", size_of::<MiValue>());
    }
}
