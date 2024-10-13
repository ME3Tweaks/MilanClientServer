//! Input / output utilities for (de)serialization logic.

use std::cell::Cell;

use crate::MiType;
use crate::read::ReadError;


/// Serial binary output over a vector.
pub struct OutputBuffer {
    /// Underlying binary buffer which actually stores output bytes.
    inner: Vec<u8>,
    /// LIFO container of offset-size pairs for markers.
    markers: Vec<(usize, usize, &'static str)>,
}

impl OutputBuffer {
    /// Constructs a new output buffer.
    pub fn new() -> Self {
        Self { inner: vec![], markers: vec![] }
    }

    /// Retrieves the underlying binary slice.
    pub fn view(&self) -> &[u8] {
        &self.inner
    }

    /// Retrieves number of markers pushed but not written.
    pub fn marker_depth(&self) -> usize {
        self.markers.len()
    }

    /// Retrieves number of bytes currently serialized.
    pub fn len(&self) -> usize {
        self.inner.len()
    }

    /// Checks if no bytes are currently serialized.
    pub fn is_empty(&self) -> bool {
        self.inner.is_empty()
    }

    /// Resets this buffer to initial empty state.
    pub fn reset(&mut self) {
        self.inner.clear();
        self.markers.clear();
    }
}

impl Default for OutputBuffer {
    fn default() -> Self {
        Self::new()
    }
}


/// Implements output operation on [`OutputBuffer`].
pub trait OutputBufferLike {
    /// Retrieves current serialization offset within the underlying buffer.
    fn pos(&self) -> usize;

    /// Retrieves next `count` byte(s) for writing, advancing output forward.
    fn get_next(&mut self, count: usize) -> &mut [u8];

    /// Retrieves next `N` byte(s) for writing as a fixed-size array, advancing output forward.
    fn get_next_array<const N: usize>(&mut self) -> &mut [u8; N] {
        self.get_next(N).try_into().unwrap()
    }

    /// Retrieves next byte for writing, advancing output forward.
    fn get_next_byte(&mut self) -> &mut u8 {
        self.get_next(1).first_mut().unwrap()
    }

    /// Remembers current offset and advances the output `count` byte(s) forward.
    fn mark_next(&mut self, count: usize, hint: &'static str);

    /// Tries to pop a marker off the stack and return the slice of the memory it marked.
    fn try_write_marked(&mut self, check: &'static str) -> Option<&mut [u8]>;
}

impl OutputBufferLike for OutputBuffer {
    fn pos(&self) -> usize {
        self.len()
    }

    fn get_next(&mut self, count: usize) -> &mut [u8] {
        let offset = self.inner.len();
        self.inner.resize(offset + count, 0_u8);
        self.inner.get_mut(offset .. offset + count).unwrap()
    }

    fn mark_next(&mut self, count: usize, hint: &'static str) {
        let offset = self.inner.len();
        self.inner.resize(offset + count, 0_u8);
        self.markers.push((offset, count, hint));
    }

    fn try_write_marked(&mut self, check: &'static str) -> Option<&mut [u8]> {
        self.markers.pop().map(|(offset, size, hint)| {
            debug_assert_eq!(check, hint, "incorrect marker popped");
            self.inner.get_mut(offset .. offset + size).unwrap()
        })
    }
}


/// Provides serial input for decoder logic.
pub trait SerialInput<'a> {
    /// Retrieves current serialization offset.
    fn pos(&self) -> usize;
    /// Retrieves the next `count` byte(s), advancing the input.
    fn get_slice(&self, count: usize) -> Result<&'a [u8], ReadError>;

    /// Retrieves the next `SIZE` byte(s) as a fixed-sized array, advancing the input.
    #[inline]
    fn get_array<const SIZE: usize>(&self) -> Result<[u8; SIZE], ReadError> {
        self.get_slice(SIZE).map(|s| s.try_into().unwrap())
    }

    /// Retrieves the next byte, advancing the input.
    #[inline]
    fn get_byte(&self) -> Result<u8, ReadError> {
        self.get_slice(1).map(|s| *s.first().unwrap())
    }

    /// Retrieves the next byte as a [`MiType`] variant.
    #[inline]
    fn get_type(&self) -> Result<MiType, ReadError> {
        let tag = self.get_byte()?;
        MiType::from_tag(tag).ok_or(ReadError::Other("invalid type tag"))
    }
}

/// Serial input over a slice.
#[derive(Clone, Debug)]
pub struct LocalSerialInput<'a> {
    slice: &'a [u8],
    offset: Cell<usize>,
}

impl<'a> LocalSerialInput<'a> {
    /// Constructs a temporary deserializer over a `slice`.
    pub fn new(slice: &'a [u8]) -> Self {
        Self { slice, offset: Cell::new(0) }
    }

    /// Retrieves processed part of the underlying slice.
    pub fn processed(&self) -> &'a [u8] {
        self.slice.get(.. self.offset.get()).unwrap()
    }

    /// Consumes this instance, returning the remaining slice.
    pub fn remaining(self) -> &'a [u8] {
        self.slice.get(self.offset.get() ..).unwrap()
    }

    /// Resets input to the beginning.
    pub fn reset(&self) {
        self.offset.set(0)
    }
}

impl<'a> SerialInput<'a> for LocalSerialInput<'a> {
    fn pos(&self) -> usize {
        self.offset.get()
    }

    fn get_slice(&self, count: usize) -> Result<&'a [u8], ReadError> {
        let offset = self.offset.get();
        let end_offset = offset + count;
        match end_offset <= self.slice.len() {
            true => {
                self.offset.set(end_offset);
                // SAFETY: We have checked end_offset to be within bounds.
                Ok(unsafe { self.slice.get_unchecked(offset .. end_offset) })
            },
            false => {
                let found = self.slice.len() - offset;
                Err(ReadError::EndOfBuffer { expected: count, found, offset })
            }
        }
    }
}
