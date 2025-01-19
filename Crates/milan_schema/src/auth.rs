//! Authentication-related messages.

use serde::{Deserialize, Serialize};
use crate::{CommonReason, TString};


/// Tries to create a new player account on the server.
///
/// The server *may* reject this request if current session is authenticated.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct CreatePlayerRequest {
    /// Identifier the player wants to be visible publicly.
    pub player_name: TString,
    /// Identifier the player wants to use for login.
    pub username: TString,
    /// Associated password cleartext.
    pub password: TString,
}

/// Response to [`CreatePlayerRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct CreatePlayerResponse {
    /// Flag indicating whether the player record was successfully created.
    pub accepted: bool,
    /// If `accepted` flag is set, a base32-encoded secret
    /// that the user needs to store in a TOTP authenticator,
    /// which can be used to restore password.
    pub totp_secret_32: TString,
    /// If `accepted` flag is not set, communicates the reason
    /// for why the player record could not be created.
    pub reject_reason: CommonReason,
}


/// Tries to authenticate the user for the duration of this session.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct LoginPlayerRequest {
    /// Internal player identifier.
    pub username: TString,
    /// Associated password cleartext.
    pub password: TString,
}

/// Response to [`LoginPlayerRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct LoginPlayerResponse {
    /// Flag indicating whether session was successfully authenticated.
    ///
    /// If this is false, server should not close the connection,
    /// but the session is still not authenticated, and any requests requiring
    /// authentication will fail with an appropriate error reason.
    pub accepted: bool,
    /// If `accepted` flag is set, the player's public name on the server.
    pub player_name: TString,
    /// If `accepted` flag is not set, communicates the reason
    /// for why the session could not be authenticated.
    pub reject_reason: CommonReason,
}


/// Tells the server to terminate session authentication.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct LogoutPlayerRequest {}

/// Response to [`LogoutPlayerRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct LogoutPlayerResponse {
    /// Original request will always succeed, but this will communicate
    /// if there was anything unexpected about it.
    pub error_reason: CommonReason,
}


/// Asks the server to update player's password.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct UpdatePasswordRequest {
    /// Internal player identifier.
    pub username: TString,
    /// A TOTP token used authenticate the player without password.
    pub totp_token: TString,
    /// Associated password cleartext to be set.
    pub new_password: TString,
}

/// Response to [`UpdatePasswordRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct UpdatePasswordResponse {
    /// Flag indicating whether the player password was successfully updated.
    pub accepted: bool,
    /// If `accepted` flag is not set, communicates the reason
    /// for why player password could not be updated.
    pub reject_reason: CommonReason,
}
