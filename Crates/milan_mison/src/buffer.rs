//! Buffer types for reading / writing.

use std::io::{Cursor, Seek};
use crate::read::MiError;


/// Serial binary input over a slice.
#[derive(Clone, Debug)]
pub struct InputBuffer<'a> {
    inner: Cursor<&'a [u8]>,
}

impl<'a> InputBuffer<'a> {
    /// Constructs a new buffer from `source` slice.
    pub fn new<'s: 'a>(source: &'s [u8]) -> Self {
        Self { inner: Cursor::new(source) }
    }

    /// Retrieves the underlying slice.
    pub fn view(&self) -> &[u8] {
        self.inner.get_ref()
    }

    /// Retrieves number of bytes in the underlying slice.
    pub fn len(&self) -> usize {
        self.inner.get_ref().len()
    }

    /// Checks if the underlying slice is empty.
    pub fn is_empty(&self) -> bool {
        self.len() == 0_usize
    }

    /// Retrieves number of bytes already read from the underlying slice.
    pub fn position(&self) -> usize {
        self.inner.position() as usize
    }

    /// Retrieves the already read part of the underlying slice.
    pub fn position_slice(&self) -> &[u8] {
        self.inner.get_ref().get(.. self.position()).unwrap()
    }

    /// Retrieves number of bytes left to read in the underlying slice.
    pub fn remaining(&self) -> usize {
        self.inner.get_ref().len() - self.position()
    }

    /// Retrieves the remaining part of the underlying slice.
    pub fn remaining_slice(&self) -> &[u8] {
        self.inner.get_ref().get(self.position() ..).unwrap()
    }

    /// Tries to move input position `distance` byte(s) forward.
    pub fn try_advance(&mut self, distance: usize) -> Option<()> {
        let position = self.position().checked_add(distance)?;
        match position <= self.len() {
            true => { self.inner.set_position(position as u64); Some(()) },
            false => None,
        }
    }

    /// Resets input position to the beginning.
    pub fn reset(&mut self) {
        self.inner.rewind().unwrap()
    }

    /// Tries to retrieve next `count` byte(s), returning [`MiError`] if unavailable.
    pub fn get_next(&mut self, count: usize) -> Result<&[u8], MiError> {
        match (self.position() + count) <= self.len() {
            true => {
                self.try_advance(count).unwrap();
                let end_range = self.position() - count ..;
                // SAFETY: Can't overrun the underlying slice by going back
                //   the same number of bytes that we advanced by two lines prior.
                Ok(unsafe { self.position_slice().get_unchecked(end_range) })
            },
            false => Err(MiError::end_of_buffer(self.position(), count, self.remaining())),
        }
    }

    /// Tries to retrieve next `N` byte(s) as a fixed array, returning [`MiError`] if unavailable.
    pub fn get_next_array<const N: usize>(&mut self) -> Result<&[u8; N], MiError> {
        self.get_next(N).map(|slice| slice.try_into().unwrap())
    }

    /// Tries to retrieve the next byte, returning [`MiError`] if unavailable.
    pub fn get_next_byte(&mut self) -> Result<u8, MiError> {
        self.get_next(1).map(|slice| slice[0])
    }
}
