//! Value serialization and deserialization.

use std::str;

use crate::MiType;
use crate::read::{MiArrayErased, MiError};


/// Trait providing generic read/write support for serializable types.
pub trait SerialValue<'a>: Sized {
    /// Associated type tag.
    const MI_TYPE: MiType;
    /// Tries to deserialize a value of this type `from` a slice.
    fn read(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError>;
    /// Tries to serialize a `value` of this type `to` a slice.
    fn write(value: Self, to: &mut [u8], offset: &mut usize) -> Result<(), MiError>;
}

impl<'a> SerialValue<'a> for () {
    const MI_TYPE: MiType = MiType::None;

    fn read(_: &'a [u8], _: &mut usize) -> Result<Self, MiError> {
        Ok(())
    }

    fn write(_: Self, _: &mut [u8], _: &mut usize) -> Result<(), MiError> {
        Ok(())
    }
}

impl<'a> SerialValue<'a> for MiArrayErased<'a> {
    const MI_TYPE: MiType = MiType::Array;

    fn read(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        MiArrayErased::read(from, offset)
    }

    fn write(_: Self, _: &mut [u8], _: &mut usize) -> Result<(), MiError> {
        unimplemented!()
    }
}

// TODO: Implement SerialValue<'a> for (Object), Array, Map.
//   These values, at least Array and Map, will need special intermediate
//   wrapper types, to avoid infinite number of type arg combinations.

impl<'a> SerialValue<'a> for i32 {
    const MI_TYPE: MiType = MiType::Int32;

    fn read(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        match from.get(.. 4) {
            Some(slice) => { *offset += 4; Ok(Self::from_be_bytes(slice.try_into().unwrap())) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len())),
        }
    }

    fn write(value: Self, to: &mut [u8], offset: &mut usize) -> Result<(), MiError> {
        match to.get_mut(.. 4) {
            Some(slice) => { *offset += 4; slice.copy_from_slice(&value.to_be_bytes()); Ok(()) },
            None => Err(MiError::end_of_buffer(*offset, 4, to.len())),
        }
    }
}

impl<'a> SerialValue<'a> for i64 {
    const MI_TYPE: MiType = MiType::Int64;

    fn read(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        match from.get(.. 8) {
            Some(slice) => { *offset += 8; Ok(Self::from_be_bytes(slice.try_into().unwrap())) },
            None => Err(MiError::end_of_buffer(*offset, 8, from.len())),
        }
    }

    fn write(value: Self, to: &mut [u8], offset: &mut usize) -> Result<(), MiError> {
        match to.get_mut(.. 8) {
            Some(slice) => { *offset += 8; slice.copy_from_slice(&value.to_be_bytes()); Ok(()) },
            None => Err(MiError::end_of_buffer(*offset, 8, to.len())),
        }
    }
}

impl<'a> SerialValue<'a> for f32 {
    const MI_TYPE: MiType = MiType::Single;

    fn read(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        match from.get(.. 4) {
            Some(slice) => { *offset += 4; Ok(Self::from_be_bytes(slice.try_into().unwrap())) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len())),
        }
    }

    fn write(value: Self, to: &mut [u8], offset: &mut usize) -> Result<(), MiError> {
        match to.get_mut(.. 4) {
            Some(slice) => { *offset += 4; slice.copy_from_slice(&value.to_be_bytes()); Ok(()) },
            None => Err(MiError::end_of_buffer(*offset, 4, to.len())),
        }
    }
}

impl<'a> SerialValue<'a> for f64 {
    const MI_TYPE: MiType = MiType::Double;

    fn read(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
            match from.get(.. 8) {
            Some(slice) => { *offset += 8; Ok(Self::from_be_bytes(slice.try_into().unwrap())) },
            None => Err(MiError::end_of_buffer(*offset, 8, from.len())),
        }
    }

    fn write(value: Self, to: &mut [u8], offset: &mut usize) -> Result<(), MiError> {
        match to.get_mut(.. 8) {
            Some(slice) => { *offset += 8; slice.copy_from_slice(&value.to_be_bytes()); Ok(()) },
            None => Err(MiError::end_of_buffer(*offset, 8, to.len())),
        }
    }
}

impl<'a> SerialValue<'a> for bool {
    const MI_TYPE: MiType = MiType::Bool;

    fn read(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        match from.first() {
            Some(&0_u8) => { *offset += 1; Ok(false) },
            Some(&1_u8) => { *offset += 1; Ok(true) },
            Some(_) => { Err(MiError::bad_value(*offset, "bool > 1")) },
            None => { Err(MiError::end_of_buffer(*offset, 1, 0)) },
        }
    }

    fn write(value: Self, to: &mut [u8], offset: &mut usize) -> Result<(), MiError> {
        match to.first_mut() {
            Some(byte) if !value => { *offset += 1; *byte = 0_u8; Ok(()) },
            Some(byte) if value => { *offset += 1; *byte = 1_u8; Ok(()) },
            None => { Err(MiError::end_of_buffer(*offset, 1, 0)) },
            _ => panic!("should be unreachable"),
        }
    }
}

impl<'a> SerialValue<'a> for &'a str {
    const MI_TYPE: MiType = MiType::String;

    fn read(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
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

    fn write(value: Self, to: &mut [u8], offset: &mut usize) -> Result<(), MiError> {
        let bytes = value.as_bytes();
        let length = (bytes.len() as u32).to_be_bytes();

        match to.get_mut(.. 4) {
            Some(slice) => { *offset += 4; slice.copy_from_slice(&length); },
            None => Err(MiError::end_of_buffer(*offset, 4, to.len()))?,
        };

        match to.get_mut(4 .. 4 + bytes.len()) {
            Some(slice) => { *offset += bytes.len(); slice.copy_from_slice(bytes); },
            None => Err(MiError::end_of_buffer(*offset, 4 + bytes.len(), to.len()))?,
        };

        match to.get_mut(4 + bytes.len()) {
            Some(null) => { *offset += 1; *null = 0_u8; Ok(()) },
            None => Err(MiError::end_of_buffer(*offset, 4 + bytes.len() + 1, to.len())),
        }
    }
}

impl<'a> SerialValue<'a> for &'a [u8] {
    const MI_TYPE: MiType = MiType::Binary;

    fn read(from: &'a [u8], offset: &mut usize) -> Result<Self, MiError> {
        let length = match from.get(.. 4) {
            Some(slice) => { *offset += 4; u32::from_be_bytes(slice.try_into().unwrap()) },
            None => Err(MiError::end_of_buffer(*offset, 4, from.len()))?,
        };

        match from.get(4 .. 4 + length as usize) {
            Some(slice) => { *offset += length as usize; Ok(slice) },
            None => Err(MiError::end_of_buffer(*offset, 4 + length as usize, from.len()))?,
        }
    }

    fn write(value: Self, to: &mut [u8], offset: &mut usize) -> Result<(), MiError> {
        let length = (value.len() as u32).to_be_bytes();

        match to.get_mut(.. 4) {
            Some(slice) => { *offset += 4; slice.copy_from_slice(&length); },
            None => Err(MiError::end_of_buffer(*offset, 4, to.len()))?,
        };

        match to.get_mut(4 .. 4 + value.len()) {
            Some(slice) => { *offset += value.len(); slice.copy_from_slice(value); Ok(()) },
            None => Err(MiError::end_of_buffer(*offset, 4 + value.len(), to.len()))?,
        }
    }
}


#[cfg(test)]
mod tests {
    use super::SerialValue;

    #[test]
    fn check_string_lifetime() {
        let source: &[u8] = b"\x00\x00\x00\x0DHello there!\x00";

        fn foo(s: &[u8]) -> &str {
            let mut offset = 0_usize;
            SerialValue::read(s, &mut offset).unwrap()
        }

        assert_eq!(foo(source).as_bytes(), &source[4..16]);
    }
}
