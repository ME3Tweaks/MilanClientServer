//! Session-related messages.

use chrono::NaiveDateTime;
use serde::{Deserialize, Serialize};

use crate::{CommonReason, CommonVersion};


/// First message sent over an established connection
/// to initiate further communication.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct BeginSessionRequest {
    /// Version of MilanHost.asi
    pub asi_version: CommonVersion,
    /// Version of ProjectMilan DLC
    pub content_version: CommonVersion,
    /// Version of MassEffect3.exe
    pub game_version: CommonVersion,
    /// Arbitrary flags
    pub devel_flags: u32,
}

/// Response to [`BeginSessionRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct BeginSessionResponse {
    /// Flag indicating whether session was successfully started.
    ///
    /// If this is false, server can be expected to have closed the connection,
    /// and no more messages should be sent or received.
    pub accepted: bool,
    /// Maximum interval, in milliseconds, that the server expects
    /// from the client's keep-alive requests.
    pub keep_alive_ms: u32,
    /// If `accepted` flag is not set, communicates the reason
    /// for why the session could not be started.
    pub reject_reason: CommonReason,
}


/// Message sent to gracefully terminate an established connection.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct EndSessionRequest {}

/// Response to [`EndSessionRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct EndSessionResponse {}


/// Message sent in a fixed interval to keep the connection alive.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct KeepAliveRequest {
    /// Current client time.
    pub timestamp: NaiveDateTime,
}

/// Response to [`KeepAliveResponse`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct KeepAliveResponse {
    /// Server-side timestamp at which the originating request arrived.
    pub request_arrived: NaiveDateTime,
    /// Client-side timestamp at which the originating request was sent.
    pub request_sent: NaiveDateTime,
}
