//! This test serializes a map of arrays structure and then deserializes it.
//! We are using generated string comparison to check that the round trip is correct.

use std::fmt::Write;

use milan_mison::MiType;
use milan_mison::read::{MiArrayView, MiDecoder, MiValue, ReadError};
use milan_mison::write::{ArrayValueMarker, MiEncoder, WriteError};


#[test]
fn roundtrip_b() {
    let bytes = serialize().unwrap();
    let string = deserialize(&bytes).unwrap();

    let expected = r#"Map 'lookup':
  mapping String -> Array
  with 2 pair(s):
    'first' -> Array of Int64:
      93284, 198263, 781
    'second' -> Array of Int32:
      <empty>
"#;

    assert_eq!(string, expected);
}

fn serialize() -> Result<Vec<u8>, WriteError> {
    let mut encoder = MiEncoder::new();
    encoder.start().map::<&str, ArrayValueMarker>(Some("lookup"))?
        // Push a key-value pair with value being an <i64> array.
        .push::<i64, _>("first", |array| {
            array
                .push(93284)?
                .push(198263)?
                .push(781)?
                .finish()
        })?
        .push::<i32, _>("second", |array| {
            // Serialize an empty array!
            array.finish()
        })?
        .finish()?;

    let (names, elements) = encoder.finish()?;
    Ok([names, elements].concat())
}

fn deserialize(slice: &[u8]) -> Result<String, ReadError> {
    let mut output = String::new();

    let decoder = MiDecoder::decode(&slice)?;
    let root = decoder.root()?;

    writeln!(output, "{} '{}':", root.value.get_type(),
             root.name.unwrap_or("<anonymous>")).unwrap();

    if let MiValue::Map(map_view) = root.value {
        writeln!(output, "  mapping {} -> {}", map_view.key_type, map_view.value_type).unwrap();
        writeln!(output, "  with {} pair(s):", map_view.count).unwrap();

        if let Some(map) = map_view.to_typed::<&str, MiArrayView>() {
            for pair in &map {
                if let Ok((key, value)) = pair {
                    writeln!(output, "    '{}' -> Array of {}:", key, value.value_type).unwrap();
                    match (value.count, value.value_type) {
                        (0, _) => {
                            writeln!(output, "      <empty>").unwrap();
                        }
                        (_, MiType::Int64) => {
                            let array = value.to_typed::<i64>().unwrap();
                            let elements = array.into_iter()
                                .map(|r| r.unwrap().to_string())
                                .collect::<Vec<_>>().join(", ");
                            writeln!(output, "      {}", elements).unwrap();
                        },
                        (_, MiType::Int32) => {
                            let array = value.to_typed::<i32>().unwrap();
                            let elements = array.into_iter()
                                .map(|r| r.unwrap().to_string())
                                .collect::<Vec<_>>().join(", ");
                            writeln!(output, "      {}", elements).unwrap();
                        },
                        _ => {},
                    }
                }
            }
        }
    }

    Ok(output)
}
