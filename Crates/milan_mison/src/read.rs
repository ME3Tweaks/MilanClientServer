//! (Almost)-zero-copy element / value deserialization.

use std::cell::Cell;
use std::fmt;
use std::marker::PhantomData;

use thiserror::Error;

use crate::MiType;
use crate::value::SerialValue;


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
        match erased.mi_type {
            t if t == Item::MI_TYPE => Ok(Self::new(erased.count, erased.init_offset, erased.init_slice)),
            _ => Err(MiError::value_type(Item::MI_TYPE, erased.mi_type)),
        }
    }

    /// Tries to deserialize an array `from` a slice.
    pub fn read<'s: 'a>(from: &'s [u8], offset: &mut usize) -> Result<Self, MiError> {
        MiArrayErased::read(from, offset).and_then(Self::view)
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
    pub fn next_item<'f: 'a>(&'f self) -> MiReadResult<Item> {
        if self.at_end() {
            // We *must* have exactly zero bytes left.
            // Even a single byte off remaining is misserialization.
            return MiReadResult::None;
        }

        // This code is quite ugly because we require internal mutability.
        // We really need it, though, to support the "while let MiReadResult(..) = ..." pattern.

        let (item_start, item_slice) = self.current.get();
        let mut next_start = item_start;

        match Item::read(item_slice, &mut next_start) {
            Ok(value) => {
                let next_slice = item_slice.get(next_start - item_start ..).unwrap();
                self.current.set((next_start, next_slice));
                MiReadResult::Ok(value)
            },
            Err(error) => MiReadResult::Err(error),
        }
    }
}

/// Type-erased representation of an [`MiType::Array`] value.
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
    pub mi_type: MiType,
    /// Initial serialization offset.
    pub init_offset: usize,
    /// Initial slice over the payload.
    pub init_slice: &'a [u8],
}

impl<'a> MiArrayErased<'a> {
    /// Tries to deserialize an array `from` a slice.
    pub fn read<'s: 'a>(from: &'s [u8], offset: &mut usize) -> Result<Self, MiError> {
        let header_start = *offset;
        let MiArrayHeader { size, count, mi_type } = MiArrayHeader::read(from, offset)?;

        let payload_start = *offset - header_start;
        let payload_end = payload_start + size as usize;

        debug_assert_eq!(payload_start, header_start + 7);

        match from.get(payload_start .. payload_end) {
            Some(init_slice) => {
                *offset += size as usize;
                let init_offset = *offset;
                Ok(Self { count: count as usize, mi_type, init_offset, init_slice })
            },
            None => Err(MiError::end_of_buffer(payload_start, payload_end, from.len()))?,
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
    pub mi_type: MiType,
}

impl MiArrayHeader {
    /// Tries to deserialize a header `from` a slice.
    pub fn read(from: &[u8], offset: &mut usize) -> Result<Self, MiError> {
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

        let mi_type = mi_type.ok_or(MiError::bad_value(*offset - 1, "invalid type"))?;
        Ok(Self { size, count, mi_type })
    }
}


#[cfg(test)]
mod tests {
    use crate::read::{MiArrayView, MiReadResult};

    #[test]
    fn test_array_lifetime() {
        let array = MiArrayView::<i32>::new(0, 0, &[]);
        assert_eq!(array.len(), 0);
        while let MiReadResult::Ok(value) = array.next_item() {
            println!("value => {}", value);
        }
    }
}
