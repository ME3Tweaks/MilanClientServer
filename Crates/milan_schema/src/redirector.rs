//! Redirector-related messages.

use serde::{Deserialize, Serialize};
use crate::{RedirectServerBundle, TVec};


/// Asks redirector for real server address.
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct QueryRedirectorRequest {}


/// Response to [`QueryRedirectorRequest`].
#[derive(Clone, Debug, Deserialize, Serialize)]
pub struct QueryRedirectorResponse {
    /// Actual server options.
    pub bundles: TVec<RedirectServerBundle, 2>,
}
