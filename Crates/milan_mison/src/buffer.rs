//! Buffer types for reading / writing.

use std::cell::Cell;
use crate::read::MiError;

#[cfg(doc)]
use crate::read::MiErrorKind;


/// Serial binary input over a slice,
/// heavily relying on interior mutability.
#[derive(Clone, Debug)]
pub struct InputBuffer<'a> {
    slice: &'a [u8],
    position: Cell<usize>,
    serial_offset: usize,
}

impl<'a> InputBuffer<'a> {
    /// Constructs a new buffer from source `slice`.
    pub fn new<'s: 'a>(slice: &'s [u8]) -> Self {
        Self::new_at(slice, 0_usize)
    }

    /// Constructs a new buffer from source `slice` with the given `serial_offset`.
    pub fn new_at<'s: 'a>(slice: &'s [u8], serial_offset: usize) -> Self {
        Self { slice, position: Cell::new(0), serial_offset }
    }

    /// Retrieves the underlying slice.
    pub fn view(&self) -> &[u8] {
        self.slice
    }

    /// Retrieves number of bytes in the underlying slice.
    pub fn len(&self) -> usize {
        self.slice.len()
    }

    /// Checks if the underlying slice is empty.
    pub fn is_empty(&self) -> bool {
        self.len() == 0_usize
    }

    /// Retrieves current serialization offset.
    pub fn serial_offset(&self) -> usize {
        self.pos() + self.serial_offset
    }

    /// Retrieves number of bytes already read from the underlying slice.
    pub fn pos(&self) -> usize {
        self.position.get()
    }

    /// Retrieves the already read part of the underlying slice.
    pub fn pos_slice(&self) -> &[u8] {
        self.slice.get(.. self.pos()).unwrap()
    }

    /// Retrieves number of bytes left to read in the underlying slice.
    pub fn remaining(&self) -> usize {
        self.len() - self.pos()
    }

    /// Retrieves the remaining part of the underlying slice.
    pub fn remaining_slice(&self) -> &[u8] {
        self.slice.get(self.pos() ..).unwrap()
    }

    /// Tries to move input position `distance` byte(s) forward.
    fn try_advance(&self, distance: usize) -> Option<usize> {
        let position = self.position.get().checked_add(distance)?;
        match position <= self.len() {
            true => { self.position.set(position); Some(position) },
            false => None,
        }
    }

    /// Gets a copy of this input buffer reset to the beginning.
    pub fn initial(&self) -> Self {
        let copy = self.clone();
        copy.reset();
        copy
    }

    /// Resets input position to the beginning.
    pub fn reset(&self) {
        self.position.set(0)
    }

    /// Tries to retrieve next `count` byte(s), returning [`MiErrorKind::EndOfBuffer`] if unavailable.
    pub fn get_next(&self, count: usize) -> Result<&[u8], MiError> {
        match self.try_advance(count) {
            Some(position) => Ok(self.slice.get(position - count .. position).unwrap()),
            None => Err(MiError::end_of_buffer(self.serial_offset(), count, self.remaining())),
        }
    }

    /// Tries to retrieve next `N` byte(s) as a fixed array, returning [`MiErrorKind::EndOfBuffer`] if unavailable.
    pub fn get_next_array<const N: usize>(&self) -> Result<[u8; N], MiError> {
        self.get_next(N).map(|slice| slice.try_into().unwrap())
    }

    /// Tries to retrieve the next byte, returning [`MiErrorKind::EndOfBuffer`] if unavailable.
    pub fn get_next_byte(&self) -> Result<u8, MiError> {
        self.get_next(1).map(|slice| slice[0])
    }

    /// Tries to retrieve next `count` byte(s) as a split-off [`InputBuffer`] instance.
    pub fn get_next_buffer(&'a self, count: usize, offset: isize) -> Result<InputBuffer<'a>, MiError> {
        let serial_offset = (self.serial_offset() as isize + offset) as usize;
        self.get_next(count).map(|slice| Self::new_at(slice, serial_offset))
    }
}


impl<'a> From<&'a [u8]> for InputBuffer<'a> {
    fn from(value: &'a [u8]) -> Self {
        Self::new(value)
    }
}
