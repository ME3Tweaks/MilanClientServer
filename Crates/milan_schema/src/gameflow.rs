//! Lobby and matchmaking -related messages.

use std::net::IpAddr;
use serde::{Deserialize, Serialize};
use crate::{CommonReason, MatchSettings, MatchStatus, MatchUnique, TString};


/// Creates a new match and, on success,
/// automatically joins the session as host player.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct CreateGameRequest {
    /// Match parameters.
    pub settings: MatchSettings,
}

/// Response to [`CreateGameRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct CreateGameResponse {
    /// Flag indicating whether a new match was successfully created.
    pub accepted: bool,
    /// If `accepted` flag is set, communicates the unique identifier
    /// that this and other clients can use to reference the new match.
    pub match_unique: MatchUnique,
    /// If `accepted` flag is not set, communicates the reason
    /// for why a new match could not be created.
    pub reject_reason: CommonReason,
}


/// Changes settings and/or status for the match
/// owned by the current client's player.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct UpdateGameRequest {
    /// New match parameters.
    pub new_settings: Option<MatchSettings>,
    /// New match status.
    pub new_status: Option<MatchStatus>,
}

/// Response to [`UpdateGameRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct UpdateGameResponse {
    /// Flag indicating whether new game parameters have been applied successfully.
    pub accepted: bool,
    /// If `accepted` flag is not set, communicates the reason
    /// for why new game parameters could not be applied.
    pub reject_reason: CommonReason,
}


/// Initiates combined search + joining a match by the current player.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct QuickGameRequest {
    /// Match parameters to search for.
    pub settings: MatchSettings,
    /// Timeout for finding a match, in milliseconds.
    pub search_timeout_ms: u32,
}

/// Response to [`QuickGameRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct QuickGameResponse {
    /// Flag indicating whether the game was found and joined on the server,
    /// and the client is allowed to start connecting to it in Unreal Engine.
    pub accepted: bool,
    /// If `accepted` flag is set, address of the match host.
    pub host_address: IpAddr,
    /// If `accepted` flag is not set, communicates the reason
    /// for why client could not join the game on the server.
    pub reject_reason: CommonReason,
}


/// Initiates joining a match by the current player.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct JoinGameRequest {
    /// The match to join.
    pub match_unique: MatchUnique,
}

/// Response to [`JoinGameRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct JoinGameResponse {
    /// Flag indicating whether the client joined the game on the server
    /// and is allowed to start connecting to it in Unreal Engine.
    pub accepted: bool,
    /// If `accepted` flag is set, address of the match host.
    pub host_address: IpAddr,
    /// If `accepted` flag is not set, communicates the reason
    /// for why client could not join the game on the server.
    pub reject_reason: CommonReason,
}


/// Initiates leaving the match associated with the current client's player.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct LeaveGameRequest {
    /// Originating reason for leaving the game.
    pub leave_reason: TString,
}

/// Response to [`LeaveGameRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct LeaveGameResponse {
    /// Original request will always succeed, but this will communicate
    /// if there was anything unexpected about it.
    pub error_reason: CommonReason,
}
