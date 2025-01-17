//! This test serializes a small MISON schema and then deserializes it.
//! As long as everything is implemented correctly, original and result contents
//! should be identical...

use milan_mison::MiType;
use milan_mison::decode::{MiDecoder, MiElement, MiObject, MiValue, ReadError};
use milan_mison::encode::{MiEncoder, ObjectValueMarker, WriteError};


#[test]
fn roundtrip_a() {
    let bytes = serialize().unwrap();
    // std::fs::write("./roundtrip_a.mison.bin", &bytes).unwrap();
    deserialize(&bytes).unwrap();
}

/// Builds and serializes an object.
fn serialize() -> Result<Vec<u8>, WriteError> {
    let mut encoder = MiEncoder::new();
    encoder.start().object(Some("root"))?
        .push(|elem| elem.value(Some("greeting"), "hello there!"))?
        .push(|elem| elem.value(Some("enabled"), true))?
        .push(|elem| {
            elem.array::<ObjectValueMarker>(Some("responses"))?
                .push(|object| {
                    object
                        .push(|inner| inner.value(Some("greeting"), "cheers"))?
                        .push(|inner| inner.value(Some("agreement"), "yep!"))?
                        .finish()
                })?
                .push(|object| {
                    object
                        .push(|inner| inner.value(Some("greeting"), "hi!"))?
                        .push(|inner| inner.value(Some("agreement"), "indeed..."))?
                        .finish()
                })?
                .finish()
        })?
        .finish()?;

    let (names, elements) = encoder.finish()?;
    Ok([names, elements].concat())
}

/// Reads and deserializes an object.
fn deserialize(slice: &[u8]) -> Result<(), ReadError> {
    let decoder = MiDecoder::decode(&slice)?;
    let root = decoder.root()?;
    assert!(matches!(root.name, Some("root")));
    assert!(matches!(root.value, MiValue::Object { .. }));

    let MiValue::Object(object) = root.value else { panic!() };
    assert_eq!(object.count, 3);

    let mut root_iter = object.into_iter();
    assert!(matches!(
        root_iter.next(),
        Some(Ok(MiElement {
            name: Some("greeting"),
            value: MiValue::String("hello there!"),
        }))
    ));
    assert!(matches!(
        root_iter.next(),
        Some(Ok(MiElement {
            name: Some("enabled"),
            value: MiValue::Bool(true),
        }))
    ));

    let MiElement { name, value } = root_iter.next().unwrap()?;
    assert!(matches!(name, Some("responses")));
    assert!(matches!(value.get_type(), MiType::Array));

    let MiValue::Array(responses) = value else { panic!() };
    assert!(matches!(responses.value_type, MiType::Object));

    let responses = responses.to_typed::<MiObject>().unwrap();
    assert_eq!(responses.count, 2);

    let mut response_iter = responses.into_iter();

    let first = response_iter.next().unwrap()?;
    {
        let mut first_iter = first.into_iter();
        assert!(matches!(
            first_iter.next(),
            Some(Ok(MiElement {
                name: Some("greeting"),
                value: MiValue::String("cheers"),
            })),
        ));
        assert!(matches!(
            first_iter.next(),
            Some(Ok(MiElement {
                name: Some("agreement"),
                value: MiValue::String("yep!"),
            })),
        ));
        assert!(first_iter.next().is_none());
    }

    let second = response_iter.next().unwrap()?;
    {
        let mut second_iter = second.into_iter();
        assert!(matches!(
            second_iter.next(),
            Some(Ok(MiElement {
                name: Some("greeting"),
                value: MiValue::String("hi!"),
            })),
        ));
        assert!(matches!(
            second_iter.next(),
            Some(Ok(MiElement {
                name: Some("agreement"),
                value: MiValue::String("indeed..."),
            })),
        ));
        assert!(second_iter.next().is_none());
    }

    assert!(response_iter.next().is_none());

    Ok(())
}
