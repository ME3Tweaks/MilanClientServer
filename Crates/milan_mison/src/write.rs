//! Reference serialization implementation.

use std::marker::PhantomData;

use crate::buffer::OutputBuffer;
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
        fn construct(buffer: &'a mut OutputBuffer) -> MiArrayBuilder<'a, Value>;
    }

    impl<'a> MiArrayBuilderFactory<'a, ArrayValueMarker> for ArrayValueMarker {
        fn construct(buffer: &'a mut OutputBuffer) -> MiArrayBuilder<'a, ArrayValueMarker> {
            MiArrayBuilder::<'a, ArrayValueMarker>::construct_with(buffer, MiType::Array)
        }
    }

    impl<'a> MiArrayBuilderFactory<'a, MapValueMarker> for MapValueMarker {
        fn construct(buffer: &'a mut OutputBuffer) -> MiArrayBuilder<'a, MapValueMarker> {
            MiArrayBuilder::<'a, MapValueMarker>::construct_with(buffer, MiType::Map)
        }
    }

    impl<'a> MiArrayBuilderFactory<'a, ObjectValueMarker> for ObjectValueMarker {
        fn construct(buffer: &'a mut OutputBuffer) -> MiArrayBuilder<'a, ObjectValueMarker> {
            MiArrayBuilder::<'a, ObjectValueMarker>::construct_with(buffer, MiType::Object)
        }
    }

    impl<'a, Value: MiWriteValue> MiArrayBuilderFactory<'a, Value> for Value {
        fn construct(buffer: &'a mut OutputBuffer) -> MiArrayBuilder<'a, Value> {
            MiArrayBuilder::<'a, Value>::construct_with(buffer, Value::MI_TYPE)
        }
    }


    /// Trait for constructing [`MiMapBuilder`] generically.
    pub trait MiMapBuilderFactory<'a, Key, Value> {
        /// Constructs a map builder with a generically-resolved key / value types.
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, Key, Value>;
    }

    impl<'a> MiMapBuilderFactory<'a, ArrayValueMarker, ArrayValueMarker> for (ArrayValueMarker, ArrayValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, ArrayValueMarker, ArrayValueMarker> {
            MiMapBuilder::<'a, ArrayValueMarker, ArrayValueMarker>::construct_with(buffer, MiType::Array, MiType::Array)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ArrayValueMarker, MapValueMarker> for (ArrayValueMarker, MapValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, ArrayValueMarker, MapValueMarker> {
            MiMapBuilder::<'a, ArrayValueMarker, MapValueMarker>::construct_with(buffer, MiType::Array, MiType::Map)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ArrayValueMarker, ObjectValueMarker> for (ArrayValueMarker, ObjectValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, ArrayValueMarker, ObjectValueMarker> {
            MiMapBuilder::<'a, ArrayValueMarker, ObjectValueMarker>::construct_with(buffer, MiType::Array, MiType::Object)
        }
    }

    impl<'a, Value: MiWriteValue> MiMapBuilderFactory<'a, ArrayValueMarker, Value> for (ArrayValueMarker, Value) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, ArrayValueMarker, Value> {
            MiMapBuilder::<'a, ArrayValueMarker, Value>::construct_with(buffer, MiType::Array, Value::MI_TYPE)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, MapValueMarker, ArrayValueMarker> for (MapValueMarker, ArrayValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, MapValueMarker, ArrayValueMarker> {
            MiMapBuilder::<'a, MapValueMarker, ArrayValueMarker>::construct_with(buffer, MiType::Map, MiType::Array)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, MapValueMarker, MapValueMarker> for (MapValueMarker, MapValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, MapValueMarker, MapValueMarker> {
            MiMapBuilder::<'a, MapValueMarker, MapValueMarker>::construct_with(buffer, MiType::Map, MiType::Map)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, MapValueMarker, ObjectValueMarker> for (MapValueMarker, ObjectValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, MapValueMarker, ObjectValueMarker> {
            MiMapBuilder::<'a, MapValueMarker, ObjectValueMarker>::construct_with(buffer, MiType::Map, MiType::Object)
        }
    }

    impl<'a, Value: MiWriteValue> MiMapBuilderFactory<'a, MapValueMarker, Value> for (MapValueMarker, Value) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, MapValueMarker, Value> {
            MiMapBuilder::<'a, MapValueMarker, Value>::construct_with(buffer, MiType::Map, Value::MI_TYPE)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ObjectValueMarker, ArrayValueMarker> for (ObjectValueMarker, ArrayValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, ObjectValueMarker, ArrayValueMarker> {
            MiMapBuilder::<'a, ObjectValueMarker, ArrayValueMarker>::construct_with(buffer, MiType::Object, MiType::Array)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ObjectValueMarker, MapValueMarker> for (ObjectValueMarker, MapValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, ObjectValueMarker, MapValueMarker> {
            MiMapBuilder::<'a, ObjectValueMarker, MapValueMarker>::construct_with(buffer, MiType::Object, MiType::Map)
        }
    }

    impl<'a> MiMapBuilderFactory<'a, ObjectValueMarker, ObjectValueMarker> for (ObjectValueMarker, ObjectValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, ObjectValueMarker, ObjectValueMarker> {
            MiMapBuilder::<'a, ObjectValueMarker, ObjectValueMarker>::construct_with(buffer, MiType::Object, MiType::Object)
        }
    }

    impl<'a, Value: MiWriteValue> MiMapBuilderFactory<'a, ObjectValueMarker, Value> for (ObjectValueMarker, Value) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, ObjectValueMarker, Value> {
            MiMapBuilder::<'a, ObjectValueMarker, Value>::construct_with(buffer, MiType::Object, Value::MI_TYPE)
        }
    }

    impl<'a, Key: MiWriteValue> MiMapBuilderFactory<'a, Key, ArrayValueMarker> for (Key, ArrayValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, Key, ArrayValueMarker> {
            MiMapBuilder::<'a, Key, ArrayValueMarker>::construct_with(buffer, Key::MI_TYPE, MiType::Array)
        }
    }

    impl<'a, Key: MiWriteValue> MiMapBuilderFactory<'a, Key, MapValueMarker> for (Key, MapValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, Key, MapValueMarker> {
            MiMapBuilder::<'a, Key, MapValueMarker>::construct_with(buffer, Key::MI_TYPE, MiType::Map)
        }
    }

    impl<'a, Key: MiWriteValue> MiMapBuilderFactory<'a, Key, ObjectValueMarker> for (Key, ObjectValueMarker) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, Key, ObjectValueMarker> {
            MiMapBuilder::<'a, Key, ObjectValueMarker>::construct_with(buffer, Key::MI_TYPE, MiType::Object)
        }
    }

    impl<'a, Key: MiWriteValue, Value: MiWriteValue> MiMapBuilderFactory<'a, Key, Value> for (Key, Value) {
        fn construct(buffer: &'a mut OutputBuffer) -> MiMapBuilder<'a, Key, Value> {
            MiMapBuilder::<'a, Key, Value>::construct_with(buffer, Key::MI_TYPE, Value::MI_TYPE)
        }
    }
}

use factories::*;


/// Provides generic value serialization.
///
/// # Remarks
/// Note that this trait is not symmetric with [`MiReadValue`].
/// While the latter supports serialization of object, array and map values,
/// this trait does not, because it is designed for usage with [`MiBuilder`] API,
/// which provides special logic for handling values of those types.
///
/// [`MiReadValue`]: crate::read::MiReadValue
pub trait MiWriteValue: Sized {
    const MI_TYPE: MiType;
    fn get_size(&self) -> usize;
    fn write_value(&self, output: &mut OutputBuffer);
}

impl MiWriteValue for () {
    const MI_TYPE: MiType = MiType::None;
    fn get_size(&self) -> usize { 0 }
    fn write_value(&self, _: &mut OutputBuffer) {}
}

impl MiWriteValue for i32 {
    const MI_TYPE: MiType = MiType::Int32;
    fn get_size(&self) -> usize { 4 }
    fn write_value(&self, output: &mut OutputBuffer) {
        output.get_next_array::<4>().copy_from_slice(&self.to_be_bytes())
    }
}

impl MiWriteValue for i64 {
    const MI_TYPE: MiType = MiType::Int64;
    fn get_size(&self) -> usize { 8 }
    fn write_value(&self, output: &mut OutputBuffer) {
        output.get_next_array::<8>().copy_from_slice(&self.to_be_bytes())
    }
}

impl MiWriteValue for f32 {
    const MI_TYPE: MiType = MiType::Single;
    fn get_size(&self) -> usize { 4 }
    fn write_value(&self, output: &mut OutputBuffer) {
        output.get_next_array::<4>().copy_from_slice(&self.to_be_bytes())
    }
}

impl MiWriteValue for f64 {
    const MI_TYPE: MiType = MiType::Double;
    fn get_size(&self) -> usize { 8 }
    fn write_value(&self, output: &mut OutputBuffer) {
        output.get_next_array::<8>().copy_from_slice(&self.to_be_bytes())
    }
}

impl MiWriteValue for bool {
    const MI_TYPE: MiType = MiType::Bool;
    fn get_size(&self) -> usize { 1 }
    fn write_value(&self, output: &mut OutputBuffer) {
        *output.get_next_byte() = if *self { 1_u8 } else { 0_u8 }
    }
}

impl MiWriteValue for &str {
    const MI_TYPE: MiType = MiType::String;
    fn get_size(&self) -> usize {
        4 + self.as_bytes().len() + 1
    }
    fn write_value(&self, output: &mut OutputBuffer) {
        let bytes = self.as_bytes();
        let length = (bytes.len() + 1) as u32;
        output.get_next_array::<4>().copy_from_slice(&length.to_be_bytes());
        output.get_next(bytes.len()).copy_from_slice(bytes);
        *output.get_next_byte() = 0_u8;
    }
}

impl MiWriteValue for &[u8] {
    const MI_TYPE: MiType = MiType::Binary;
    fn get_size(&self) -> usize {
        4 + self.len()
    }
    fn write_value(&self, output: &mut OutputBuffer) {
        output.get_next_array::<4>().copy_from_slice(&self.len().to_be_bytes());
        output.get_next(self.len()).copy_from_slice(self);
    }
}


/// Encapsulates construction of a top-level element.
pub struct MiBuilder {
    buffer: OutputBuffer,
}

impl MiBuilder {
    /// Constructs a new, empty builder.
    pub fn new() -> Self { Self { buffer: OutputBuffer::new() } }

    /// Retrieves a view of the currently serialized contents.
    pub fn view(&self) -> &[u8] {
        debug_assert_eq!(
            self.buffer.marker_count(), 0,
            "one or more markers have not been popped"
        );
        self.buffer.view()
    }

    /// Resets the builder to its initial empty state, preserving internal allocations.
    pub fn reset(&mut self) { self.buffer.reset(); }

    /// Constructs a builder instance for the root element.
    pub fn root(&mut self) -> MiElementBuilder<'_> {
        MiElementBuilder { buffer: &mut self.buffer }
    }
}

impl Default for MiBuilder {
    fn default() -> Self {
        Self::new()
    }
}


/// Encapsulates construction of an element.
pub struct MiElementBuilder<'a> {
    buffer: &'a mut OutputBuffer,
}

impl<'a> MiElementBuilder<'a> {
    fn write_header_internal(&mut self, r#type: MiType, name: Option<&str>) {
        *self.buffer.get_next_byte() = MiType::build_tag(r#type, name.is_none());

        if let Some(name) = name {
            assert!(name.is_ascii());
            assert!(!name.is_empty() && name.len() < 8);

            let mut buffer = [0_u8; 8];
            let slice = unsafe { buffer.get_unchecked_mut(.. name.len()) };
            slice.copy_from_slice(name.as_bytes());

            self.buffer.get_next_array::<8>().copy_from_slice(&buffer);
        }
    }

    /// Writes a *simple* value.
    pub fn value<Value>(&mut self, name: Option<&str>, value: Value)
    where
        Value: MiWriteValue,
    {
        self.write_header_internal(Value::MI_TYPE, name);
        value.write_value(self.buffer);
    }

    /// Starts writing an *array* value, effectively consuming builder.
    pub fn array<'buf, Value>(&'buf mut self, name: Option<&str>) -> MiArrayBuilder<'buf, Value>
    where
        'a: 'buf,
        Value: MiArrayBuilderFactory<'buf, Value>,
    {
        self.write_header_internal(MiType::Array, name);
        Value::construct(self.buffer)
    }

    /// Starts writing a *map* value, effectively consuming builder.
    pub fn map<'buf, Key, Value>(&'buf mut self, name: Option<&str>) -> MiMapBuilder<'buf, Key, Value>
    where
        'a: 'buf,
        (Key, Value): MiMapBuilderFactory<'buf, Key, Value>,
    {
        self.write_header_internal(MiType::Map, name);
        <(Key, Value)>::construct(self.buffer)
    }

    /// Starts writing an *object* value, effectively consuming builder.
    pub fn object(&mut self, name: Option<&str>) -> MiObjectBuilder<'_> {
        self.write_header_internal(MiType::Object, name);
        MiObjectBuilder::construct(self.buffer)
    }
}


/// Writes contents of an array value.
pub struct MiArrayBuilder<'a, Value> {
    buffer: &'a mut OutputBuffer,
    item_count: usize,
    start_offset: usize,
    _marker: PhantomData<[Value]>,
}

impl<'a, Value> MiArrayBuilder<'a, Value> {
    fn construct_with(buffer: &'a mut OutputBuffer, value_type: MiType) -> Self {
        buffer.mark_next(4, "size");
        buffer.mark_next(2, "count");

        *buffer.get_next_byte() = value_type as u8;

        let (item_count, start_offset) = (0, buffer.len());
        Self { buffer, item_count, start_offset, _marker: PhantomData }
    }

    /// Finalizes array serialization.
    pub fn finish(&mut self) {
        let size = self.buffer.len() - self.start_offset;
        self.buffer.try_write_marked("count").unwrap()
            .copy_from_slice(&(self.item_count as u16).to_be_bytes());
        self.buffer.try_write_marked("size").unwrap()
            .copy_from_slice(&(size as u32).to_be_bytes());
    }
}

impl<'a> MiArrayBuilder<'a, ArrayValueMarker> {
    /// Serializes an *array* value constructed by a `callback`, appending it to this array.
    pub fn push<Item, Callback>(&mut self, callback: Callback) -> &mut Self
    where
        for<'f> Item: MiArrayBuilderFactory<'f, Item>,
        Callback: Fn(&mut MiArrayBuilder<'_, Item>),
    {
        callback(&mut Item::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a> MiArrayBuilder<'a, MapValueMarker> {
    /// Serializes a *map* value constructed by a `callback`, appending it to this array.
    pub fn push<Key, Value, Callback>(&mut self, callback: Callback) -> &mut Self
    where
        for<'f> (Key, Value): MiMapBuilderFactory<'f, Key, Value>,
        Callback: Fn(&mut MiMapBuilder<'_, Key, Value>),
    {
        callback(&mut <(Key, Value)>::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a> MiArrayBuilder<'a, ObjectValueMarker> {
    /// Serializes an *object* value constructed by a `callback`, appending it to this array.
    pub fn push<Callback>(&mut self, callback: Callback) -> &mut Self
    where
        Callback: Fn(&mut MiObjectBuilder<'_>),
    {
        callback(&mut MiObjectBuilder::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a, Value: MiWriteValue> MiArrayBuilder<'a, Value> {
    /// Serializes a *simple* value, appending it to this array.
    pub fn push(&mut self, value: Value) -> &mut Self {
        value.write_value(self.buffer);
        self.item_count += 1;
        self
    }
}


/// Writes contents of a map value.
pub struct MiMapBuilder<'a, Key, Value> {
    buffer: &'a mut OutputBuffer,
    item_count: usize,
    start_offset: usize,
    _marker: PhantomData<[(Key, Value)]>,
}

impl<'a, Key, Value> MiMapBuilder<'a, Key, Value> {
    fn construct_with(
        buffer: &'a mut OutputBuffer,
        key_type: MiType,
        value_type: MiType,
    ) -> Self {
        buffer.mark_next(4, "size");
        buffer.mark_next(2, "count");

        *buffer.get_next_byte() = key_type as u8;
        *buffer.get_next_byte() = value_type as u8;

        let (item_count, start_offset) = (0, buffer.len());
        Self { buffer, item_count, start_offset, _marker: PhantomData }
    }

    /// Finalizes map serialization.
    pub fn finish(&mut self) {
        let size = self.buffer.len() - self.start_offset;
        self.buffer.try_write_marked("count").unwrap()
            .copy_from_slice(&(self.item_count as u16).to_be_bytes());
        self.buffer.try_write_marked("size").unwrap()
            .copy_from_slice(&(size as u32).to_be_bytes());
    }
}

impl<'a> MiMapBuilder<'a, ArrayValueMarker, ArrayValueMarker> {
    /// Serializes a pair of *array* key and *array* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<Key, Value, CbKey, CbValue>(&mut self, key: CbKey, value: CbValue) -> &mut Self
    where
        for<'k> Key: MiArrayBuilderFactory<'k, Key>,
        for<'v> Value: MiArrayBuilderFactory<'v, Value>,
        CbKey: Fn(&mut MiArrayBuilder<'_, Key>),
        CbValue: Fn(&mut MiArrayBuilder<'_, Value>),
    {
        key(&mut Key::construct(self.buffer));
        value(&mut Value::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a> MiMapBuilder<'a, ArrayValueMarker, MapValueMarker> {
    /// Serializes a pair of *array* key and *map* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<Key, ValueKey, ValueValue, CbKey, CbValue>(&mut self, key: CbKey, value: CbValue) -> &mut Self
    where
        for<'k> Key: MiArrayBuilderFactory<'k, Key>,
        for<'v> (ValueKey, ValueValue): MiMapBuilderFactory<'v, ValueKey, ValueValue>,
        CbKey: Fn(&mut MiArrayBuilder<'_, Key>),
        CbValue: Fn(&mut MiMapBuilder<'_, ValueKey, ValueValue>),
    {
        key(&mut Key::construct(self.buffer));
        value(&mut <(ValueKey, ValueValue)>::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a> MiMapBuilder<'a, ArrayValueMarker, ObjectValueMarker> {
    /// Serializes a pair of *array* key and *object* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<Key, CbKey, CbValue>(&mut self, key: CbKey, value: CbValue) -> &mut Self
    where
        for<'k> Key: MiArrayBuilderFactory<'k, Key>,
        CbKey: Fn(&mut MiArrayBuilder<'_, Key>),
        CbValue: Fn(&mut MiObjectBuilder<'_>),
    {
        key(&mut Key::construct(self.buffer));
        value(&mut MiObjectBuilder::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a, Value: MiWriteValue> MiMapBuilder<'a, ArrayValueMarker, Value> {
    /// Serializes a pair of an *array* key and a *simple* `value`, key
    /// being constructed by the `key` callback, appending it to this map.
    pub fn push<Key, CbKey>(&mut self, key: CbKey, value: Value) -> &mut Self
    where
        for<'k> Key: MiArrayBuilderFactory<'k, Key>,
        CbKey: Fn(&mut MiArrayBuilder<'_, Key>),
    {
        key(&mut Key::construct(self.buffer));
        value.write_value(self.buffer);
        self.item_count += 1;
        self
    }
}

impl<'a> MiMapBuilder<'a, MapValueMarker, ArrayValueMarker> {
    /// Serializes a pair of *map* key and *array* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<KeyKey, KeyValue, Value, CbKey, CbValue>(&mut self, key: CbKey, value: CbValue) -> &mut Self
    where
        for<'k> (KeyKey, KeyValue): MiMapBuilderFactory<'k, KeyKey, KeyValue>,
        for<'v> Value: MiArrayBuilderFactory<'v, Value>,
        CbKey: Fn(&mut MiMapBuilder<'_, KeyKey, KeyValue>),
        CbValue: Fn(&mut MiArrayBuilder<'_, Value>),
    {
        key(&mut <(KeyKey, KeyValue)>::construct(self.buffer));
        value(&mut Value::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a> MiMapBuilder<'a, MapValueMarker, MapValueMarker> {
    /// Serializes a pair of *map* key and *map* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<KeyKey, KeyValue, ValueKey, ValueValue, CbKey, CbValue>(&mut self, key: CbKey, value: CbValue) -> &mut Self
    where
        for<'k> (KeyKey, KeyValue): MiMapBuilderFactory<'k, KeyKey, KeyValue>,
        for<'v> (ValueKey, ValueValue): MiMapBuilderFactory<'v, ValueKey, ValueValue>,
        CbKey: Fn(&mut MiMapBuilder<'_, KeyKey, KeyValue>),
        CbValue: Fn(&mut MiMapBuilder<'_, ValueKey, ValueValue>),
    {
        key(&mut <(KeyKey, KeyValue)>::construct(self.buffer));
        value(&mut <(ValueKey, ValueValue)>::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a> MiMapBuilder<'a, MapValueMarker, ObjectValueMarker> {
    /// Serializes a pair of *map* key and *object* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<KeyKey, KeyValue, CbKey, CbValue>(&mut self, key: CbKey, value: CbValue) -> &mut Self
    where
        for<'k> (KeyKey, KeyValue): MiMapBuilderFactory<'k, KeyKey, KeyValue>,
        CbKey: Fn(&mut MiMapBuilder<'_, KeyKey, KeyValue>),
        CbValue: Fn(&mut MiObjectBuilder<'_>),
    {
        key(&mut <(KeyKey, KeyValue)>::construct(self.buffer));
        value(&mut MiObjectBuilder::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a, Value: MiWriteValue> MiMapBuilder<'a, MapValueMarker, Value> {
    /// Serializes a pair of a *map* key and a *simple* `value`, key
    /// being constructed by the `key` callback, appending it to this map.
    pub fn push<KeyKey, KeyValue, CbKey>(&mut self, key: CbKey, value: Value) -> &mut Self
    where
        for<'k> (KeyKey, KeyValue): MiMapBuilderFactory<'k, KeyKey, KeyValue>,
        CbKey: Fn(&mut MiMapBuilder<'_, KeyKey, KeyValue>),
    {
        key(&mut <(KeyKey, KeyValue)>::construct(self.buffer));
        value.write_value(self.buffer);
        self.item_count += 1;
        self
    }
}

impl<'a> MiMapBuilder<'a, ObjectValueMarker, ArrayValueMarker> {
    /// Serializes a pair of *object* key and *array* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<Value, CbKey, CbValue>(&mut self, key: CbKey, value: CbValue) -> &mut Self
    where
        for<'v> Value: MiArrayBuilderFactory<'v, Value>,
        CbKey: Fn(&mut MiObjectBuilder<'_>),
        CbValue: Fn(&mut MiArrayBuilder<'_, Value>),
    {
        key(&mut MiObjectBuilder::construct(self.buffer));
        value(&mut Value::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a> MiMapBuilder<'a, ObjectValueMarker, MapValueMarker> {
    /// Serializes a pair of *object* key and *map* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<ValueKey, ValueValue, CbKey, CbValue>(&mut self, key: CbKey, value: CbValue) -> &mut Self
    where
        for<'v> (ValueKey, ValueValue): MiMapBuilderFactory<'v, ValueKey, ValueValue>,
        CbKey: Fn(&mut MiObjectBuilder<'_>),
        CbValue: Fn(&mut MiMapBuilder<'_, ValueKey, ValueValue>),
    {
        key(&mut MiObjectBuilder::construct(self.buffer));
        value(&mut <(ValueKey, ValueValue)>::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a> MiMapBuilder<'a, ObjectValueMarker, ObjectValueMarker> {
    /// Serializes a pair of *object* key and *object* value, constructed
    /// by `key` and `value` callbacks respectively, appending it to this map.
    pub fn push<CbKey, CbValue>(&mut self, key: CbKey, value: CbValue) -> &mut Self
    where
        CbKey: Fn(&mut MiObjectBuilder<'_>),
        CbValue: Fn(&mut MiObjectBuilder<'_>),
    {
        key(&mut MiObjectBuilder::construct(self.buffer));
        value(&mut MiObjectBuilder::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a, Value: MiWriteValue> MiMapBuilder<'a, ObjectValueMarker, Value> {
    /// Serializes a pair of an *object* key and a *simple* `value`, key
    /// being constructed by the `key` callback, appending it to this map.
    pub fn push<CbKey>(&mut self, key: CbKey, value: Value) -> &mut Self
    where
        CbKey: Fn(&mut MiObjectBuilder<'_>),
    {
        key(&mut MiObjectBuilder::construct(self.buffer));
        value.write_value(self.buffer);
        self.item_count += 1;
        self
    }
}

impl<'a, Key: MiWriteValue> MiMapBuilder<'a, Key, ArrayValueMarker> {
    /// Serializes a pair of a *simple* `key` and an *array* value, value
    /// being constructed by the `value` callback, appending it to this map.
    pub fn push<Value, CbValue>(&mut self, key: Key, value: CbValue) -> &mut Self
    where
        for<'v> Value: MiArrayBuilderFactory<'v, Value>,
        CbValue: Fn(&mut MiArrayBuilder<'_, Value>),
    {
        key.write_value(self.buffer);
        value(&mut Value::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a, Key: MiWriteValue> MiMapBuilder<'a, Key, MapValueMarker> {
    /// Serializes a pair of a *simple* `key` and a *map* value, value
    /// being constructed by the `value` callback, appending it to this map.
    pub fn push<ValueKey, ValueValue, CbValue>(&mut self, key: Key, value: CbValue) -> &mut Self
    where
        for<'v> (ValueKey, ValueValue): MiMapBuilderFactory<'v, ValueKey, ValueValue>,
        CbValue: Fn(&mut MiMapBuilder<'_, ValueKey, ValueValue>),
    {
        key.write_value(self.buffer);
        value(&mut <(ValueKey, ValueValue)>::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a, Key: MiWriteValue> MiMapBuilder<'a, Key, ObjectValueMarker> {
    /// Serializes a pair of a *simple* `key` and an *object* value, value
    /// being constructed by the `value` callback, appending it to this map.
    pub fn push<CbValue>(&mut self, key: Key, value: CbValue) -> &mut Self
    where
        CbValue: Fn(&mut MiObjectBuilder<'_>),
    {
        key.write_value(self.buffer);
        value(&mut MiObjectBuilder::construct(self.buffer));
        self.item_count += 1;
        self
    }
}

impl<'a, Key: MiWriteValue, Value: MiWriteValue> MiMapBuilder<'a, Key, Value> {
    /// Serializes a pair of a *simple* `key` and a *simple* `value`, appending it to this map.
    pub fn push(&mut self, key: Key, value: Value) -> &mut Self {
        key.write_value(self.buffer);
        value.write_value(self.buffer);
        self.item_count += 1;
        self
    }
}


/// Writes contents of an object value.
pub struct MiObjectBuilder<'a> {
    buffer: &'a mut OutputBuffer,
    item_count: usize,
    start_offset: usize,
}

impl<'a> MiObjectBuilder<'a> {
    fn construct(buffer: &'a mut OutputBuffer) -> Self {
        buffer.mark_next(4, "size");
        buffer.mark_next(2, "count");
        let (item_count, start_offset) = (0, buffer.len());
        Self { buffer, item_count, start_offset }
    }

    /// Finalizes object serialization.
    pub fn finish(&mut self) {
        let size = self.buffer.len() - self.start_offset;
        self.buffer.try_write_marked("count").unwrap()
            .copy_from_slice(&(self.item_count as u16).to_be_bytes());
        self.buffer.try_write_marked("size").unwrap()
            .copy_from_slice(&(size as u32).to_be_bytes());
    }

    /// Serializes an element (i.e. field) constructed by a `callback`, appending it to this object.
    pub fn push<Callback>(&mut self, callback: Callback) -> &mut Self
    where
        Callback: Fn(&mut MiElementBuilder<'_>),
    {
        callback(&mut MiElementBuilder { buffer: self.buffer });
        self.item_count += 1;
        self
    }
}


#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_array_build() {
        let mut builder = MiBuilder::new();
        builder.root().array::<i32>(Some("ARRAY"))
            .push(2).push(4).push(8)
            .finish();

        let expected = b"\x02ARRAY\x00\x00\x00\x00\x00\x00\x0C\x00\x03\x04\x00\x00\x00\x02\x00\x00\x00\x04\x00\x00\x00\x08";
        assert_eq!(expected, builder.view());

        // std::fs::write("./test_1.mison.bin", builder.view()).unwrap()
    }

    #[test]
    fn test_array_of_arrays() {
        let mut builder = MiBuilder::new();
        builder.root().array::<ArrayValueMarker>(Some("ARRAYS"))
            .push::<i32, _>(|inner| { inner.push(2).push(4).finish() })
            .push::<&str, _>(|inner| { inner.push("cheers").push("hello").finish() })
            .finish();

        let expected_str = "02 41 52 52 41 59 53 00 00 00 00 00 2B 00 02 02 00 00 00 08 00 02 04 00 00 00 02 00 00 00 04 00 00 00 15 00 02 09 00 00 00 07 63 68 65 65 72 73 00 00 00 00 06 68 65 6C 6C 6F 00";
        let expected_slice = expected_str.split(' ').map(|s| u8::from_str_radix(s, 16).unwrap()).collect::<Vec<_>>();

        assert_eq!(&expected_slice, builder.view());

        // std::fs::write("./test_2.mison.bin", builder.view()).unwrap()
    }

    #[test]
    fn test_map_build() {
        let mut builder = MiBuilder::new();
        builder.root().map::<ArrayValueMarker, &str>(None)
            .push::<i32, _>(|inner| { inner.push(1).push(2).finish() }, "two")
            .push::<bool, _>(|inner| { inner.push(true).finish() }, "one")
            .finish();

        // std::fs::write("./test_3.mison.bin", builder.view()).unwrap()
    }

    #[test]
    fn test_object_build() {
        let kits = vec!["kit-1", "kit-4"];

        let mut builder = MiBuilder::new();
        builder.root().object(None)
            .push(|elem| elem.value(Some("map"), "some-level"))
            .push(|elem| elem.value(Some("prvcy"), true))
            .push(|elem| {
                let mut array = elem.array::<&str>(Some("chars"));
                for kit in &kits { array.push(kit); }
                array.finish()
            })
            .push(|elem| {
                elem.map::<&str, i64>(Some("achvmnt"))
                    .push("tracker-one", 832)
                    .push("tracker-two", 0)
                    .push("custom-tracker-x", 1234)
                    .push("custom-tracker-y", -9)
                    .finish()
            })
            .finish();

        // std::fs::write("./test_4.mison.bin", builder.view()).unwrap()
    }
}
