#![allow(dead_code)]
#![allow(unsafe_code)]

//! MISON - Milan Structured Object Notation.


use std::fmt;
use std::str::FromStr;


pub mod read;
pub mod value;


/// Serializable value types.
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Ord, PartialOrd, Hash)]
pub enum MiType {
    /// Absence of value.
    #[default]
    None = 0x00,
    /// Collection of named values.
    Object = 0x01,
    /// Collection of anonymous values.
    Array = 0x02,
    /// Collection of value-value pairs, where both sides may or may not be anonymous.
    Map = 0x03,
    /// Signed 32-bit integer value.
    Int32 = 0x04,
    /// Signed 64-bit integer value.
    Int64 = 0x05,
    /// Single-precision 32-bit floating-point value.
    Single = 0x06,
    /// Double-precision 64-bit floating-point value.
    Double = 0x07,
    /// Boolean value.
    Bool = 0x08,
    /// Null-terminated UTF-8 string.
    String = 0x09,
    /// Untyped binary blob.
    Binary = 0x0A,
}

impl MiType {
    /// Tries to convert a byte `tag` into a type variant.
    pub fn from_tag(tag: u8) -> Option<Self> {
        match tag {
            0x00 => Some(Self::None),
            0x01 => Some(Self::Object),
            0x02 => Some(Self::Array),
            0x03 => Some(Self::Map),
            0x04 => Some(Self::Int32),
            0x05 => Some(Self::Int64),
            0x06 => Some(Self::Single),
            0x07 => Some(Self::Double),
            0x08 => Some(Self::Bool),
            0x09 => Some(Self::String),
            0x0A => Some(Self::Binary),
            _ => None,
        }
    }

    /// Tries to split a byte `tag` into a type variant and `anonymous` flag.
    pub fn split_tag(tag: u8) -> Option<(Self, bool)> {
        let anonymous = (tag & 0x80) != 0x00;
        Self::from_tag(tag & 0x7f).map(|v| (v, anonymous ))
    }

    /// Tries to retrieve a content-independent size that a value of this type has.
    ///
    /// # Returns
    /// This function returns [`None`] if this type has no fixed size,
    /// in which case the variable size must be read from the first four bytes
    /// of a value payload.
    pub fn fixed_size(self) -> Option<usize> {
        match self {
            MiType::None => Some(0),
            MiType::Int32 => Some(4),
            MiType::Int64 => Some(8),
            MiType::Single => Some(4),
            MiType::Double => Some(8),
            MiType::Bool => Some(1),
            _ => None,
        }
    }

    /// Gets name of a type variant.
    pub fn as_str(self) -> &'static str {
        match self {
            MiType::None => "None",
            MiType::Object => "Object",
            MiType::Array => "Array",
            MiType::Map => "Map",
            MiType::Int32 => "Int32",
            MiType::Int64 => "Int64",
            MiType::Single => "Single",
            MiType::Double => "Double",
            MiType::Bool => "Bool",
            MiType::String => "String",
            MiType::Binary => "Binary",
        }
    }
}

impl From<MiType> for &'static str {
    fn from(value: MiType) -> Self {
        value.as_str()
    }
}

impl TryFrom<u8> for MiType {
    type Error = ();
    fn try_from(value: u8) -> Result<Self, Self::Error> {
        Self::from_tag(value).ok_or(())
    }
}

impl FromStr for MiType {
    type Err = ();
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "Null" => Ok(Self::None),
            "Object" => Ok(Self::Object),
            "Array" => Ok(Self::Array),
            "Map" => Ok(Self::Map),
            "Int32" => Ok(Self::Int32),
            "Int64" => Ok(Self::Int64),
            "Single" => Ok(Self::Single),
            "Double" => Ok(Self::Double),
            "Bool" => Ok(Self::Bool),
            "String" => Ok(Self::String),
            "Binary" => Ok(Self::Binary),
            _ => Err(()),
        }
    }
}

impl fmt::Display for MiType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.as_str())
    }
}
