//! Deserializable value representation.

use std::str;
use crate::buffer::InputBuffer;
use crate::MiType;
use crate::read::{MiArrayErased, MiError, MiMapErased, MiObjectView};


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

impl<'a> From<&MiValue<'a>> for MiType {
    fn from(value: &MiValue<'a>) -> Self {
        value.get_type()
    }
}


/// Provides generic value deserialization.
pub trait MiDeserialValue<'a>: Sized {
    /// Associated type tag.
    const MI_TYPE: MiType;

    /// Tries to deserialize a value of this type from an `input` buffer.
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError>;
}

impl<'a> MiDeserialValue<'a> for () {
    const MI_TYPE: MiType = MiType::None;
    fn read_value(_: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        Ok(())
    }
}

impl<'a> MiDeserialValue<'a> for MiObjectView<'a> {
    const MI_TYPE: MiType = MiType::Object;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        MiObjectView::read_from(input)
    }
}

impl<'a> MiDeserialValue<'a> for MiArrayErased<'a> {
    const MI_TYPE: MiType = MiType::Array;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        MiArrayErased::read_from(input)
    }
}

impl<'a> MiDeserialValue<'a> for MiMapErased<'a> {
    const MI_TYPE: MiType = MiType::Map;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        MiMapErased::read_from(input)
    }
}

impl<'a> MiDeserialValue<'a> for i32 {
    const MI_TYPE: MiType = MiType::Int32;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        input.get_next_array::<4>().map(i32::from_be_bytes)
    }
}

impl<'a> MiDeserialValue<'a> for i64 {
    const MI_TYPE: MiType = MiType::Int64;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        input.get_next_array::<8>().map(i64::from_be_bytes)
    }
}

impl<'a> MiDeserialValue<'a> for f32 {
    const MI_TYPE: MiType = MiType::Single;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        input.get_next_array::<4>().map(f32::from_be_bytes)
    }
}

impl<'a> MiDeserialValue<'a> for f64 {
    const MI_TYPE: MiType = MiType::Double;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        input.get_next_array::<8>().map(f64::from_be_bytes)
    }
}

impl<'a> MiDeserialValue<'a> for bool {
    const MI_TYPE: MiType = MiType::Bool;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        match input.get_next_byte()? {
            value @ (0_u8 | 1_u8) => Ok(value != 0_u8),
            _ => Err(MiError::bad_value(input.pos() - 1, "bool > 1")),
        }
    }
}

impl<'a> MiDeserialValue<'a> for &'a str {
    const MI_TYPE: MiType = MiType::String;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        let length = input.get_next_array::<4>().map(u32::from_be_bytes)?;

        let str_start = input.pos();
        let str_slice = input.get_next(length as usize)?;

        let slice = match str_slice.split_last() {
            Some((&0_u8, slice)) => slice,
            Some(..) => Err(MiError::bad_value(str_start, "string not terminated"))?,
            None => Err(MiError::bad_value(str_start, "null string size"))?,
        };

        str::from_utf8(slice).map_err(move |_| MiError::bad_value(str_start, "not utf-8"))
    }
}

impl<'a> MiDeserialValue<'a> for &'a [u8] {
    const MI_TYPE: MiType = MiType::Binary;
    fn read_value(input: &'a InputBuffer<'a>) -> Result<Self, MiError> {
        let length = input.get_next_array::<4>().map(u32::from_be_bytes)?;
        let slice = input.get_next(length as usize)?;
        Ok(slice)
    }
}


#[cfg(test)]
mod tests {
    use crate::buffer::InputBuffer;
    use crate::read::{MiElement, MiReadResult};
    use crate::value::{MiValue, MiDeserialValue};

    #[test]
    fn test_string_read() {
        let source = b"\x00\x00\x00\x0DHello there!\x00";

        fn foo<'a>(input: &'a InputBuffer<'a>) -> &'a str {
            MiDeserialValue::read_value(input).unwrap()
        }

        let buffer = InputBuffer::new(source);
        assert_eq!(foo(&buffer).as_bytes(), &source[4..16]);
    }

    #[test]
    fn test_object_read() {
        let source = b"\x81\x00\x00\x00\x27\x00\x02\x09STRING\x00\x00\x00\x00\x00\x0DHello there!\x00\x06FLOAT\x00\x00\x00\x3F\x80\x00\x00";
        let buffer = InputBuffer::new(source);

        let document = MiElement::read_from(&buffer).unwrap();
        assert!(document.name.is_none());
        assert_eq!(document.offset, 0x00);

        let MiValue::Object(root) = document.value else { panic!() };
        assert_eq!(root.len(), 0x02);

        assert!(matches!(
            root.next_element(),
            MiReadResult::Ok(MiElement {
                offset: 0x07,
                name: Some("STRING"),
                value: MiValue::String("Hello there!"),
            }),
        ));

        assert!(matches!(
            root.next_element(),
            MiReadResult::Ok(MiElement {
                offset: 0x21,
                name: Some("FLOAT"),
                value: MiValue::Single(1.0),
            }),
        ));
    }
}
