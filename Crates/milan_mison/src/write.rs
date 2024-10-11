//! Reference implementation for MISON serialization.

use std::marker::PhantomData;
use thiserror::Error;

use crate::buffer::{OutputBuffer, OutputBufferLike};
use crate::MiType;


/// Marker types for serializing array / map / object values via array / map builders.
pub mod markers {
    /// Item type marker for array keys/values.
    pub struct ArrayValueMarker;
    /// Item type marker for map keys/values.
    pub struct MapValueMarker;
    /// Item type marker for object keys/values.
    pub struct ObjectValueMarker;
}

pub use markers::{ArrayValueMarker, MapValueMarker, ObjectValueMarker};


/// Traits implementing generic array / map builder construction.
pub mod factories {
    use crate::write::*;

    /// Trait for constructing [`MiArrayBuilder`] generically.
    pub trait MiArrayBuilderFactory<'a, Value> {
        /// Constructs an array builder with a generically-resolved item type.
        fn construct(encoder: &'a mut MiEncoder) -> MiArrayBuilder<'a, Value>;
    }

    impl<'a> MiArrayBuilderFactory<'a, ArrayValueMarker> for ArrayValueMarker {
        fn construct(encoder: &'a mut MiEncoder) -> MiArrayBuilder<'a, ArrayValueMarker> {
            MiArrayBuilder::<'a, ArrayValueMarker>::construct_with(encoder, MiType::Array)
        }
    }

    impl<'a> MiArrayBuilderFactory<'a, MapValueMarker> for MapValueMarker {
        fn construct(encoder: &'a mut MiEncoder) -> MiArrayBuilder<'a, MapValueMarker> {
            MiArrayBuilder::<'a, MapValueMarker>::construct_with(encoder, MiType::Map)
        }
    }

    impl<'a> MiArrayBuilderFactory<'a, ObjectValueMarker> for ObjectValueMarker {
        fn construct(encoder: &'a mut MiEncoder) -> MiArrayBuilder<'a, ObjectValueMarker> {
            MiArrayBuilder::<'a, ObjectValueMarker>::construct_with(encoder, MiType::Object)
        }
    }

    impl<'a, Value: MiWriteValue> MiArrayBuilderFactory<'a, Value> for Value {
        fn construct(encoder: &'a mut MiEncoder) -> MiArrayBuilder<'a, Value> {
            MiArrayBuilder::<'a, Value>::construct_with(encoder, Value::MI_TYPE)
        }
    }


    /// Trait for constructing [`MiMapBuilder`] generically.
    pub trait MiMapBuilderFactory<'a, Key, Value> {
        /// Constructs a map builder with a generically-resolved key / value types.
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, Key, Value>;
    }

    impl<'a> MiMapBuilderFactory<'a, ArrayValueMarker, ArrayValueMarker> for (ArrayValueMarker, ArrayValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, ArrayValueMarker, ArrayValueMarker> {
            MiMapBuilder::<'a, ArrayValueMarker, ArrayValueMarker>::construct_with(encoder, MiType::Array, MiType::Array)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ArrayValueMarker, MapValueMarker> for (ArrayValueMarker, MapValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, ArrayValueMarker, MapValueMarker> {
            MiMapBuilder::<'a, ArrayValueMarker, MapValueMarker>::construct_with(encoder, MiType::Array, MiType::Map)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ArrayValueMarker, ObjectValueMarker> for (ArrayValueMarker, ObjectValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, ArrayValueMarker, ObjectValueMarker> {
            MiMapBuilder::<'a, ArrayValueMarker, ObjectValueMarker>::construct_with(encoder, MiType::Array, MiType::Object)
        }
    }

    impl<'a, Value: MiWriteValue> MiMapBuilderFactory<'a, ArrayValueMarker, Value> for (ArrayValueMarker, Value) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, ArrayValueMarker, Value> {
            MiMapBuilder::<'a, ArrayValueMarker, Value>::construct_with(encoder, MiType::Array, Value::MI_TYPE)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, MapValueMarker, ArrayValueMarker> for (MapValueMarker, ArrayValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, MapValueMarker, ArrayValueMarker> {
            MiMapBuilder::<'a, MapValueMarker, ArrayValueMarker>::construct_with(encoder, MiType::Map, MiType::Array)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, MapValueMarker, MapValueMarker> for (MapValueMarker, MapValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, MapValueMarker, MapValueMarker> {
            MiMapBuilder::<'a, MapValueMarker, MapValueMarker>::construct_with(encoder, MiType::Map, MiType::Map)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, MapValueMarker, ObjectValueMarker> for (MapValueMarker, ObjectValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, MapValueMarker, ObjectValueMarker> {
            MiMapBuilder::<'a, MapValueMarker, ObjectValueMarker>::construct_with(encoder, MiType::Map, MiType::Object)
        }
    }

    impl<'a, Value: MiWriteValue> MiMapBuilderFactory<'a, MapValueMarker, Value> for (MapValueMarker, Value) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, MapValueMarker, Value> {
            MiMapBuilder::<'a, MapValueMarker, Value>::construct_with(encoder, MiType::Map, Value::MI_TYPE)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ObjectValueMarker, ArrayValueMarker> for (ObjectValueMarker, ArrayValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, ObjectValueMarker, ArrayValueMarker> {
            MiMapBuilder::<'a, ObjectValueMarker, ArrayValueMarker>::construct_with(encoder, MiType::Object, MiType::Array)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ObjectValueMarker, MapValueMarker> for (ObjectValueMarker, MapValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, ObjectValueMarker, MapValueMarker> {
            MiMapBuilder::<'a, ObjectValueMarker, MapValueMarker>::construct_with(encoder, MiType::Object, MiType::Map)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ObjectValueMarker, ObjectValueMarker> for (ObjectValueMarker, ObjectValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, ObjectValueMarker, ObjectValueMarker> {
            MiMapBuilder::<'a, ObjectValueMarker, ObjectValueMarker>::construct_with(encoder, MiType::Object, MiType::Object)
        }
    }

    impl<'a, Value: MiWriteValue> MiMapBuilderFactory<'a, ObjectValueMarker, Value> for (ObjectValueMarker, Value) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, ObjectValueMarker, Value> {
            MiMapBuilder::<'a, ObjectValueMarker, Value>::construct_with(encoder, MiType::Object, Value::MI_TYPE)
        }
    }

    impl<'a, Key: MiWriteValue> MiMapBuilderFactory<'a, Key, ArrayValueMarker> for (Key, ArrayValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, Key, ArrayValueMarker> {
            MiMapBuilder::<'a, Key, ArrayValueMarker>::construct_with(encoder, Key::MI_TYPE, MiType::Array)
        }
    }

    impl<'a, Key: MiWriteValue> MiMapBuilderFactory<'a, Key, MapValueMarker> for (Key, MapValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, Key, MapValueMarker> {
            MiMapBuilder::<'a, Key, MapValueMarker>::construct_with(encoder, Key::MI_TYPE, MiType::Map)
        }
    }

    impl<'a, Key: MiWriteValue> MiMapBuilderFactory<'a, Key, ObjectValueMarker> for (Key, ObjectValueMarker) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, Key, ObjectValueMarker> {
            MiMapBuilder::<'a, Key, ObjectValueMarker>::construct_with(encoder, Key::MI_TYPE, MiType::Object)
        }
    }

    impl<'a, Key: MiWriteValue, Value: MiWriteValue> MiMapBuilderFactory<'a, Key, Value> for (Key, Value) {
        fn construct(encoder: &'a mut MiEncoder) -> MiMapBuilder<'a, Key, Value> {
            MiMapBuilder::<'a, Key, Value>::construct_with(encoder, Key::MI_TYPE, Value::MI_TYPE)
        }
    }
}

use factories::*;


/// Errors produced by builder interface.
#[derive(Clone, Debug, Error)]
pub enum MiWriteError {
    #[error("element name was not ascii")]
    NameInvalidEncoding,
    #[error("element name too long")]
    NameTooLong,
}


/// Provides generic value serialization.
///
/// # Remarks
/// Note that this trait is not symmetric with [`MiReadValue`].
/// While the latter supports serialization of object, array and map values,
/// this trait does not, because it is designed for usage with [`MiEncoder`] API,
/// which provides special logic for handling values of those types.
///
/// [`MiReadValue`]: crate::read::MiReadValue
pub trait MiWriteValue: Sized {
    /// Associated type marker.
    const MI_TYPE: MiType;

    /// Retrieves number of bytes this value would take in serialized form.
    fn get_size(&self) -> usize;

    /// Tries to serialize a value of this type to an `output` buffer.
    fn write_value(&self, output: &mut impl OutputBufferLike) -> Result<(), MiWriteError>;
}

impl MiWriteValue for () {
    const MI_TYPE: MiType = MiType::None;
    fn get_size(&self) -> usize { 0 }
    fn write_value(&self, _: &mut impl OutputBufferLike) -> Result<(), MiWriteError> { Ok(()) }
}

impl MiWriteValue for i32 {
    const MI_TYPE: MiType = MiType::Int32;
    fn get_size(&self) -> usize { 4 }
    fn write_value(&self, output: &mut impl OutputBufferLike) -> Result<(), MiWriteError> {
        output.get_next_array::<4>().copy_from_slice(&self.to_be_bytes());
        Ok(())
    }
}

impl MiWriteValue for i64 {
    const MI_TYPE: MiType = MiType::Int64;
    fn get_size(&self) -> usize { 8 }
    fn write_value(&self, output: &mut impl OutputBufferLike) -> Result<(), MiWriteError> {
        output.get_next_array::<8>().copy_from_slice(&self.to_be_bytes());
        Ok(())
    }
}

impl MiWriteValue for f32 {
    const MI_TYPE: MiType = MiType::Single;
    fn get_size(&self) -> usize { 4 }
    fn write_value(&self, output: &mut impl OutputBufferLike) -> Result<(), MiWriteError> {
        output.get_next_array::<4>().copy_from_slice(&self.to_be_bytes());
        Ok(())
    }
}

impl MiWriteValue for f64 {
    const MI_TYPE: MiType = MiType::Double;
    fn get_size(&self) -> usize { 8 }
    fn write_value(&self, output: &mut impl OutputBufferLike) -> Result<(), MiWriteError> {
        output.get_next_array::<8>().copy_from_slice(&self.to_be_bytes());
        Ok(())
    }
}

impl MiWriteValue for bool {
    const MI_TYPE: MiType = MiType::Bool;
    fn get_size(&self) -> usize { 1 }
    fn write_value(&self, output: &mut impl OutputBufferLike) -> Result<(), MiWriteError> {
        *output.get_next_byte() = if *self { 1_u8 } else { 0_u8 };
        Ok(())
    }
}

impl MiWriteValue for &str {
    const MI_TYPE: MiType = MiType::String;
    fn get_size(&self) -> usize {
        4 + self.as_bytes().len() + 1
    }
    fn write_value(&self, output: &mut impl OutputBufferLike) -> Result<(), MiWriteError> {
        let bytes = self.as_bytes();
        let length = (bytes.len() + 1) as u32;
        output.get_next_array::<4>().copy_from_slice(&length.to_be_bytes());
        output.get_next(bytes.len()).copy_from_slice(bytes);
        *output.get_next_byte() = 0_u8;
        Ok(())
    }
}

impl MiWriteValue for &[u8] {
    const MI_TYPE: MiType = MiType::Binary;
    fn get_size(&self) -> usize {
        4 + self.len()
    }
    fn write_value(&self, output: &mut impl OutputBufferLike) -> Result<(), MiWriteError> {
        output.get_next_array::<4>().copy_from_slice(&self.len().to_be_bytes());
        output.get_next(self.len()).copy_from_slice(self);
        Ok(())
    }
}


/// Encapsulates serialization of a document.
pub struct MiEncoder {
    /// Stores serialized element contents.
    pub(crate) buffer: OutputBuffer,
}

impl MiEncoder {
    /// Constructs a new, empty builder.
    pub fn new() -> Self {
        Self {
            buffer: OutputBuffer::new(),
        }
    }

    /// Retrieves a view of currently serialized contents even if
    /// serialization is not yet complete, which means that one or more
    /// output markers have not been written to yet.
    ///
    /// # Safety
    /// Only call this when you are confident that all
    /// elements, including the root one, have been finalized.
    pub unsafe fn view(&self) -> &[u8] {
        self.buffer.view()
    }

    /// Retrieves a view of currently serialized contents.
    pub fn view_checked(&self) -> Option<&[u8]> {
        match self.buffer.marker_depth() {
            0 => Some(unsafe { self.view() }),
            _ => None,
        }
    }

    /// Resets the builder to its initial empty state, preserving internal allocations.
    pub fn reset(&mut self) {
        self.buffer.reset();
    }

    /// Constructs a builder instance for the root element.
    pub fn make_root(&mut self) -> MiElementBuilder<'_> {
        MiElementBuilder { encoder: self }
    }
}

impl Default for MiEncoder {
    fn default() -> Self {
        Self::new()
    }
}

impl OutputBufferLike for MiEncoder {
    fn pos(&self) -> usize { self.buffer.pos() }
    fn get_next(&mut self, count: usize) -> &mut [u8] { self.buffer.get_next(count) }
    fn mark_next(&mut self, count: usize, hint: &'static str) { self.buffer.mark_next(count, hint) }
    fn try_write_marked(&mut self, check: &'static str) -> Option<&mut [u8]> { self.buffer.try_write_marked(check) }
}


/// Writes contents of an element (i.e. tag + name + value).
pub struct MiElementBuilder<'a> {
    encoder: &'a mut MiEncoder,
}

impl<'a> MiElementBuilder<'a> {
    fn write_header_internal(
        &mut self,
        r#type: MiType,
        name: Option<&str>,
    ) -> Result<(), MiWriteError> {
        *self.encoder.get_next_byte() =
            MiType::build_tag(r#type, name.is_none());

        if let Some(name) = name {
            if !name.is_ascii() { Err(MiWriteError::NameInvalidEncoding)?; }
            if name.is_empty() || name.len() >= 8 { Err(MiWriteError::NameTooLong)?; }

            let mut buffer = [0_u8; 8];
            let slice = unsafe { buffer.get_unchecked_mut(.. name.len()) };
            slice.copy_from_slice(name.as_bytes());

            self.encoder.get_next_array::<8>().copy_from_slice(&buffer);
        }

        Ok(())
    }

    /// Writes a *simple* value.
    pub fn value<Value>(&mut self, name: Option<&str>, value: Value) -> Result<(), MiWriteError>
    where
        Value: MiWriteValue,
    {
        self.write_header_internal(Value::MI_TYPE, name)?;
        value.write_value(self.encoder)?;
        Ok(())
    }

    /// Starts writing an *array* value, effectively consuming builder.
    pub fn array<'buf, Value>(
        &'buf mut self,
        name: Option<&str>,
    ) -> Result<MiArrayBuilder<'buf, Value>, MiWriteError>
    where
        'a: 'buf,
        Value: MiArrayBuilderFactory<'buf, Value>,
    {
        self.write_header_internal(MiType::Array, name)?;
        Ok(Value::construct(self.encoder))
    }

    /// Starts writing a *map* value, effectively consuming builder.
    pub fn map<'buf, Key, Value>(
        &'buf mut self,
        name: Option<&str>,
    ) -> Result<MiMapBuilder<'buf, Key, Value>, MiWriteError>
    where
        'a: 'buf,
        (Key, Value): MiMapBuilderFactory<'buf, Key, Value>,
    {
        self.write_header_internal(MiType::Map, name)?;
        Ok(<(Key, Value)>::construct(self.encoder))
    }

    /// Starts writing an *object* value, effectively consuming builder.
    pub fn object(&mut self, name: Option<&str>) -> Result<MiObjectBuilder<'_>, MiWriteError> {
        self.write_header_internal(MiType::Object, name)?;
        Ok(MiObjectBuilder::construct(self.encoder))
    }
}


/// Writes contents of an array value.
pub struct MiArrayBuilder<'a, Value> {
    encoder: &'a mut MiEncoder,
    item_count: usize,
    start_offset: usize,
    _marker: PhantomData<[Value]>,
}

impl<'a, Value> MiArrayBuilder<'a, Value> {
    fn construct_with(encoder: &'a mut MiEncoder, value_type: MiType) -> Self {
        encoder.mark_next(4, "size");
        encoder.mark_next(2, "count");

        *encoder.get_next_byte() = value_type as u8;

        let (item_count, start_offset) = (0, encoder.pos());
        Self { encoder, item_count, start_offset, _marker: PhantomData }
    }

    /// Finalizes array serialization.
    pub fn finish(&mut self) -> Result<(), MiWriteError> {
        let count = self.item_count as u16;
        let size = (self.encoder.pos() - self.start_offset) as u32;
        self.encoder.try_write_marked("count").unwrap().copy_from_slice(&count.to_be_bytes());
        self.encoder.try_write_marked("size").unwrap().copy_from_slice(&size.to_be_bytes());
        Ok(())
    }
}

impl<'a> MiArrayBuilder<'a, ArrayValueMarker> {
    /// Serializes an *array* value constructed by a `callback`, appending it to this array.
    pub fn push<Item, Callback>(&mut self, callback: Callback) -> Result<&mut Self, MiWriteError>
    where
        for<'f> Item: MiArrayBuilderFactory<'f, Item>,
        Callback: Fn(&mut MiArrayBuilder<'_, Item>) -> Result<(), MiWriteError>,
    {
        callback(&mut Item::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiArrayBuilder<'a, MapValueMarker> {
    /// Serializes a *map* value constructed by a `callback`, appending it to this array.
    pub fn push<Key, Value, Callback>(&mut self, callback: Callback) -> Result<&mut Self, MiWriteError>
    where
        for<'f> (Key, Value): MiMapBuilderFactory<'f, Key, Value>,
        Callback: Fn(&mut MiMapBuilder<'_, Key, Value>) -> Result<(), MiWriteError>,
    {
        callback(&mut <(Key, Value)>::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiArrayBuilder<'a, ObjectValueMarker> {
    /// Serializes an *object* value constructed by a `callback`, appending it to this array.
    pub fn push<Callback>(&mut self, callback: Callback) -> Result<&mut Self, MiWriteError>
    where
        Callback: Fn(&mut MiObjectBuilder<'_>) -> Result<(), MiWriteError>,
    {
        callback(&mut MiObjectBuilder::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a, Value: MiWriteValue> MiArrayBuilder<'a, Value> {
    /// Serializes a *simple* value, appending it to this array.
    pub fn push(&mut self, value: Value) -> Result<&mut Self, MiWriteError> {
        value.write_value(self.encoder)?;
        self.item_count += 1;
        Ok(self)
    }
}


/// Writes contents of a map value.
pub struct MiMapBuilder<'a, Key, Value> {
    encoder: &'a mut MiEncoder,
    item_count: usize,
    start_offset: usize,
    _marker: PhantomData<[(Key, Value)]>,
}

impl<'a, Key, Value> MiMapBuilder<'a, Key, Value> {
    fn construct_with(
        encoder: &'a mut MiEncoder,
        key_type: MiType,
        value_type: MiType,
    ) -> Self {
        encoder.mark_next(4, "size");
        encoder.mark_next(2, "count");

        *encoder.get_next_byte() = key_type as u8;
        *encoder.get_next_byte() = value_type as u8;

        let (item_count, start_offset) = (0, encoder.pos());
        Self { encoder, item_count, start_offset, _marker: PhantomData }
    }

    /// Finalizes map serialization.
    pub fn finish(&mut self) -> Result<(), MiWriteError> {
        let count = self.item_count as u16;
        let size = (self.encoder.pos() - self.start_offset) as u32;
        self.encoder.try_write_marked("count").unwrap().copy_from_slice(&count.to_be_bytes());
        self.encoder.try_write_marked("size").unwrap().copy_from_slice(&size.to_be_bytes());
        Ok(())
    }
}

impl<'a> MiMapBuilder<'a, ArrayValueMarker, ArrayValueMarker> {
    /// Serializes a pair of *array* key and *array* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<Key, Value, CbKey, CbValue>(
        &mut self, key: CbKey, value: CbValue
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'k> Key: MiArrayBuilderFactory<'k, Key>,
        for<'v> Value: MiArrayBuilderFactory<'v, Value>,
        CbKey: Fn(&mut MiArrayBuilder<'_, Key>) -> Result<(), MiWriteError>,
        CbValue: Fn(&mut MiArrayBuilder<'_, Value>) -> Result<(), MiWriteError>,
    {
        key(&mut Key::construct(self.encoder))?;
        value(&mut Value::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiMapBuilder<'a, ArrayValueMarker, MapValueMarker> {
    /// Serializes a pair of *array* key and *map* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<Key, ValueKey, ValueValue, CbKey, CbValue>(
        &mut self, key: CbKey, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'k> Key: MiArrayBuilderFactory<'k, Key>,
        for<'v> (ValueKey, ValueValue): MiMapBuilderFactory<'v, ValueKey, ValueValue>,
        CbKey: Fn(&mut MiArrayBuilder<'_, Key>) -> Result<(), MiWriteError>,
        CbValue: Fn(&mut MiMapBuilder<'_, ValueKey, ValueValue>) -> Result<(), MiWriteError>,
    {
        key(&mut Key::construct(self.encoder))?;
        value(&mut <(ValueKey, ValueValue)>::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiMapBuilder<'a, ArrayValueMarker, ObjectValueMarker> {
    /// Serializes a pair of *array* key and *object* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<Key, CbKey, CbValue>(
        &mut self, key: CbKey, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'k> Key: MiArrayBuilderFactory<'k, Key>,
        CbKey: Fn(&mut MiArrayBuilder<'_, Key>) -> Result<(), MiWriteError>,
        CbValue: Fn(&mut MiObjectBuilder<'_>) -> Result<(), MiWriteError>,
    {
        key(&mut Key::construct(self.encoder))?;
        value(&mut MiObjectBuilder::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a, Value: MiWriteValue> MiMapBuilder<'a, ArrayValueMarker, Value> {
    /// Serializes a pair of an *array* key and a *simple* `value`, key
    /// being constructed by the `key` callback, appending it to this map.
    pub fn push<Key, CbKey>(
        &mut self, key: CbKey, value: Value,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'k> Key: MiArrayBuilderFactory<'k, Key>,
        CbKey: Fn(&mut MiArrayBuilder<'_, Key>) -> Result<(), MiWriteError>,
    {
        key(&mut Key::construct(self.encoder))?;
        value.write_value(self.encoder)?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiMapBuilder<'a, MapValueMarker, ArrayValueMarker> {
    /// Serializes a pair of *map* key and *array* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<KeyKey, KeyValue, Value, CbKey, CbValue>(
        &mut self, key: CbKey, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'k> (KeyKey, KeyValue): MiMapBuilderFactory<'k, KeyKey, KeyValue>,
        for<'v> Value: MiArrayBuilderFactory<'v, Value>,
        CbKey: Fn(&mut MiMapBuilder<'_, KeyKey, KeyValue>) -> Result<(), MiWriteError>,
        CbValue: Fn(&mut MiArrayBuilder<'_, Value>) -> Result<(), MiWriteError>,
    {
        key(&mut <(KeyKey, KeyValue)>::construct(self.encoder))?;
        value(&mut Value::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiMapBuilder<'a, MapValueMarker, MapValueMarker> {
    /// Serializes a pair of *map* key and *map* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<KeyKey, KeyValue, ValueKey, ValueValue, CbKey, CbValue>(
        &mut self, key: CbKey, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'k> (KeyKey, KeyValue): MiMapBuilderFactory<'k, KeyKey, KeyValue>,
        for<'v> (ValueKey, ValueValue): MiMapBuilderFactory<'v, ValueKey, ValueValue>,
        CbKey: Fn(&mut MiMapBuilder<'_, KeyKey, KeyValue>) -> Result<(), MiWriteError>,
        CbValue: Fn(&mut MiMapBuilder<'_, ValueKey, ValueValue>) -> Result<(), MiWriteError>,
    {
        key(&mut <(KeyKey, KeyValue)>::construct(self.encoder))?;
        value(&mut <(ValueKey, ValueValue)>::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiMapBuilder<'a, MapValueMarker, ObjectValueMarker> {
    /// Serializes a pair of *map* key and *object* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<KeyKey, KeyValue, CbKey, CbValue>(
        &mut self, key: CbKey, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'k> (KeyKey, KeyValue): MiMapBuilderFactory<'k, KeyKey, KeyValue>,
        CbKey: Fn(&mut MiMapBuilder<'_, KeyKey, KeyValue>) -> Result<(), MiWriteError>,
        CbValue: Fn(&mut MiObjectBuilder<'_>) -> Result<(), MiWriteError>,
    {
        key(&mut <(KeyKey, KeyValue)>::construct(self.encoder))?;
        value(&mut MiObjectBuilder::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a, Value: MiWriteValue> MiMapBuilder<'a, MapValueMarker, Value> {
    /// Serializes a pair of a *map* key and a *simple* `value`, key
    /// being constructed by the `key` callback, appending it to this map.
    pub fn push<KeyKey, KeyValue, CbKey>(
        &mut self, key: CbKey, value: Value,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'k> (KeyKey, KeyValue): MiMapBuilderFactory<'k, KeyKey, KeyValue>,
        CbKey: Fn(&mut MiMapBuilder<'_, KeyKey, KeyValue>) -> Result<(), MiWriteError>,
    {
        key(&mut <(KeyKey, KeyValue)>::construct(self.encoder))?;
        value.write_value(self.encoder)?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiMapBuilder<'a, ObjectValueMarker, ArrayValueMarker> {
    /// Serializes a pair of *object* key and *array* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<Value, CbKey, CbValue>(
        &mut self, key: CbKey, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'v> Value: MiArrayBuilderFactory<'v, Value>,
        CbKey: Fn(&mut MiObjectBuilder<'_>) -> Result<(), MiWriteError>,
        CbValue: Fn(&mut MiArrayBuilder<'_, Value>) -> Result<(), MiWriteError>,
    {
        key(&mut MiObjectBuilder::construct(self.encoder))?;
        value(&mut Value::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiMapBuilder<'a, ObjectValueMarker, MapValueMarker> {
    /// Serializes a pair of *object* key and *map* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<ValueKey, ValueValue, CbKey, CbValue>(
        &mut self, key: CbKey, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'v> (ValueKey, ValueValue): MiMapBuilderFactory<'v, ValueKey, ValueValue>,
        CbKey: Fn(&mut MiObjectBuilder<'_>) -> Result<(), MiWriteError>,
        CbValue: Fn(&mut MiMapBuilder<'_, ValueKey, ValueValue>) -> Result<(), MiWriteError>,
    {
        key(&mut MiObjectBuilder::construct(self.encoder))?;
        value(&mut <(ValueKey, ValueValue)>::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a> MiMapBuilder<'a, ObjectValueMarker, ObjectValueMarker> {
    /// Serializes a pair of *object* key and *object* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<CbKey, CbValue>(
        &mut self, key: CbKey, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        CbKey: Fn(&mut MiObjectBuilder<'_>) -> Result<(), MiWriteError>,
        CbValue: Fn(&mut MiObjectBuilder<'_>) -> Result<(), MiWriteError>,
    {
        key(&mut MiObjectBuilder::construct(self.encoder))?;
        value(&mut MiObjectBuilder::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a, Value: MiWriteValue> MiMapBuilder<'a, ObjectValueMarker, Value> {
    /// Serializes a pair of an *object* key and a *simple* `value`, key
    /// being constructed by the `key` callback, appending it to this map.
    pub fn push<CbKey>(
        &mut self, key: CbKey, value: Value,
    ) -> Result<&mut Self, MiWriteError>
    where
        CbKey: Fn(&mut MiObjectBuilder<'_>) -> Result<(), MiWriteError>,
    {
        key(&mut MiObjectBuilder::construct(self.encoder))?;
        value.write_value(self.encoder)?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a, Key: MiWriteValue> MiMapBuilder<'a, Key, ArrayValueMarker> {
    /// Serializes a pair of a *simple* `key` and an *array* value, value
    /// being constructed by the `value` callback, appending it to this map.
    pub fn push<Value, CbValue>(
        &mut self, key: Key, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'v> Value: MiArrayBuilderFactory<'v, Value>,
        CbValue: Fn(&mut MiArrayBuilder<'_, Value>) -> Result<(), MiWriteError>,
    {
        key.write_value(self.encoder)?;
        value(&mut Value::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a, Key: MiWriteValue> MiMapBuilder<'a, Key, MapValueMarker> {
    /// Serializes a pair of a *simple* `key` and a *map* value, value
    /// being constructed by the `value` callback, appending it to this map.
    pub fn push<ValueKey, ValueValue, CbValue>(
        &mut self, key: Key, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        for<'v> (ValueKey, ValueValue): MiMapBuilderFactory<'v, ValueKey, ValueValue>,
        CbValue: Fn(&mut MiMapBuilder<'_, ValueKey, ValueValue>) -> Result<(), MiWriteError>,
    {
        key.write_value(self.encoder)?;
        value(&mut <(ValueKey, ValueValue)>::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a, Key: MiWriteValue> MiMapBuilder<'a, Key, ObjectValueMarker> {
    /// Serializes a pair of a *simple* `key` and an *object* value, value
    /// being constructed by the `value` callback, appending it to this map.
    pub fn push<CbValue>(
        &mut self, key: Key, value: CbValue,
    ) -> Result<&mut Self, MiWriteError>
    where
        CbValue: Fn(&mut MiObjectBuilder<'_>) -> Result<(), MiWriteError>,
    {
        key.write_value(self.encoder)?;
        value(&mut MiObjectBuilder::construct(self.encoder))?;
        self.item_count += 1;
        Ok(self)
    }
}

impl<'a, Key: MiWriteValue, Value: MiWriteValue> MiMapBuilder<'a, Key, Value> {
    /// Serializes a pair of a *simple* `key` and a *simple* `value`, appending it to this map.
    pub fn push(&mut self, key: Key, value: Value) -> Result<&mut Self, MiWriteError> {
        key.write_value(self.encoder)?;
        value.write_value(self.encoder)?;
        self.item_count += 1;
        Ok(self)
    }
}


/// Writes contents of an object value.
pub struct MiObjectBuilder<'a> {
    encoder: &'a mut MiEncoder,
    item_count: usize,
    start_offset: usize,
}

impl<'a> MiObjectBuilder<'a> {
    fn construct(encoder: &'a mut MiEncoder) -> Self {
        encoder.mark_next(4, "size");
        encoder.mark_next(2, "count");
        let (item_count, start_offset) = (0, encoder.pos());
        Self { encoder, item_count, start_offset }
    }

    /// Finalizes object serialization.
    pub fn finish(&mut self) -> Result<(), MiWriteError> {
        let count = self.item_count as u16;
        let size = (self.encoder.pos() - self.start_offset) as u32;
        self.encoder.try_write_marked("count").unwrap().copy_from_slice(&count.to_be_bytes());
        self.encoder.try_write_marked("size").unwrap().copy_from_slice(&size.to_be_bytes());
        Ok(())
    }

    /// Serializes an element (i.e. field) constructed by a `callback`, appending it to this object.
    pub fn push<Callback>(&mut self, callback: Callback) -> Result<&mut Self, MiWriteError>
    where
        Callback: Fn(&mut MiElementBuilder<'_>) -> Result<(), MiWriteError>,
    {
        callback(&mut MiElementBuilder { encoder: self.encoder })?;
        self.item_count += 1;
        Ok(self)
    }
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_array_build() {
        let mut builder = MiEncoder::new();
        builder.make_root()
            .array::<i32>(Some("ARRAY")).unwrap()
            .push(2).unwrap()
            .push(4).unwrap()
            .push(8).unwrap()
            .finish().unwrap();

        let expected = b"\x02ARRAY\x00\x00\x00\x00\x00\x00\x0C\x00\x03\x04\x00\x00\x00\x02\x00\x00\x00\x04\x00\x00\x00\x08";
        assert_eq!(expected, builder.view_checked().unwrap());

        // std::fs::write("./test_1.mison.bin", builder.view_checked().unwrap()).unwrap()
    }

    #[test]
    fn test_array_of_arrays() {
        let mut builder = MiEncoder::new();
        builder.make_root()
            .array::<ArrayValueMarker>(Some("ARRAYS")).unwrap()
            .push::<i32, _>(|inner| { inner.push(2)?.push(4)?.finish() }).unwrap()
            .push::<&str, _>(|inner| { inner.push("cheers")?.push("hello")?.finish() }).unwrap()
            .finish().unwrap();

        let expected_str = "02 41 52 52 41 59 53 00 00 00 00 00 2B 00 02 02 00 00 00 08 00 02 04 00 00 00 02 00 00 00 04 00 00 00 15 00 02 09 00 00 00 07 63 68 65 65 72 73 00 00 00 00 06 68 65 6C 6C 6F 00";
        let expected_slice = expected_str.split(' ').map(|s| u8::from_str_radix(s, 16).unwrap()).collect::<Vec<_>>();

        assert_eq!(&expected_slice, builder.view_checked().unwrap());

        // std::fs::write("./test_2.mison.bin", builder.view_checked().unwrap()).unwrap()
    }

    #[test]
    fn test_map_build() {
        let mut builder = MiEncoder::new();
        builder.make_root()
            .map::<ArrayValueMarker, &str>(None).unwrap()
            .push::<i32, _>(|inner| { inner.push(1).unwrap().push(2).unwrap().finish() }, "two").unwrap()
            .push::<bool, _>(|inner| { inner.push(true).unwrap().finish() }, "one").unwrap()
            .finish().unwrap();

        // std::fs::write("./test_3.mison.bin", builder.view_checked().unwrap()).unwrap()
    }

    #[test]
    fn test_object_build() {
        let kits = vec!["kit-1", "kit-4"];

        let mut builder = MiEncoder::new();
        builder.make_root()
            .object(None).unwrap()
            .push(|elem| elem.value(Some("map"), "some-level")).unwrap()
            .push(|elem| elem.value(Some("prvcy"), true)).unwrap()
            .push(|elem| {
                let mut array = elem.array::<&str>(Some("chars"))?;
                for kit in &kits { array.push(kit)?; }
                array.finish()
            }).unwrap()
            .push(|elem| {
                elem.map::<&str, i64>(Some("achvmnt"))?
                    .push("tracker-one", 832)?
                    .push("tracker-two", 0)?
                    .push("custom-tracker-x", 1234)?
                    .push("custom-tracker-y", -9)?
                    .finish()
            }).unwrap()
            .finish()
            .unwrap();

        // std::fs::write("./test_4.mison.bin", builder.view_checked().unwrap()).unwrap()
    }
}
