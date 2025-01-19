#![allow(dead_code)]
//! Milan Schema - message types for client-server communication.

use std::net::IpAddr;
use serde::{Deserialize, Serialize};
use uuid::Uuid;


pub mod auth;
pub mod gameflow;
pub mod redirector;
pub mod session;


/// Feature-selected string type.
#[cfg(feature = "inline")]
pub type TString = compact_str::CompactString;
/// Feature-selected string type.
#[cfg(not(feature = "inline"))]
pub type TString = std::string::String;

/// Feature-selected vector type.
#[cfg(feature = "inline")]
pub type TVec<T, const INLINE: usize> = smallvec::SmallVec<[T; INLINE]>;
/// Feature-selected vector type.
#[cfg(not(feature = "inline"))]
pub type TVec<T, const INLINE: usize> = std::vec::Vec<T>;


/// Reason for an error condition in a response.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct CommonReason {
    /// String reference into ProjectMilan's TLK.
    pub reference: i32,
    /// Formatting parameters for the string reference.
    pub params: TVec<TString, 2>,
    /// Fallback, non-localized error text.
    pub text: TString,
}


/// Version description, somewhat resembling semver.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct CommonVersion {
    pub major: u8,
    pub minor: u8,
    pub patch: u8,
}


/// Combination of match settings.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct MatchSettings {
    pub difficulty: Option<TString>,
    pub faction: Option<TString>,
    pub map: Option<TString>,
}


/// Combination of match status parameters.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub enum MatchStatus {
    /// Players are waiting for the match start.
    Lobby {
        /// Whether the countdown has begun.
        ready: bool,
    },
    /// Players are loading, or have loaded, and the first wave has yet to begin.
    PreMatch,
    /// Players are playing the match (the first wave has begun, the last wave has yet to end).
    Wave {
        /// Current wave index, zero being first wave.
        current: u8,
        /// Total wave count in the active wavelist.
        count: u8,
    },
    /// Players are loading into next lobby.
    PostMatch {
        /// Whether the match ended with "mission success".
        success: bool,
        /// Whether the match ended with "mission failure".
        failure: bool,
        /// Duration of the match in milliseconds.
        duration_ms: u64,
    },
}


/// Unique identifier for a match.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct MatchUnique(pub Uuid);


/// Server record returned by the redirector.
#[derive(Clone, Debug, Deserialize, Serialize, Eq, PartialEq)]
pub struct RedirectServerBundle {
    /// Brief ASCII label describing the server bundle.
    pub code: TString,
    /// Address of the authentication / matchmaking server.
    pub game_address: IpAddr,
    /// Port of the authentication / matchmaking server.
    pub game_port: u16,
}
