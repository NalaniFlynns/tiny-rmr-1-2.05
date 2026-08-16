#pragma once
// Encrypted-firmware flash helper: detects .fwsec containers, asks the UI
// for the unlock credential, decrypts to a private temp file, and guarantees
// the plaintext temp file is deleted after flashing.
#include <QString>
#include <functional>
#include <string>
#include <vector>
#include "fwsec_container.h"

struct FwsecUnlockResult {
    bool cancelled = false;
    std::string password;          // AUTH_PASSWORD
    std::vector<unsigned char> fido_secret;  // AUTH_FIDO2 / AUTH_PASSKEY (32B)
    std::string error;
};

// If path is a .fwsec container: probe -> ask() -> decrypt to temp file.
// Otherwise plainPath = path unchanged. Returns false on any failure.
bool fwsec_prepare_flash(const QString& path,
                         const std::function<FwsecUnlockResult(const fwsec::FwsecFileInfo&)>& ask,
                         QString& plainPath, QString& err);

// Delete the temporary plaintext file (no-op for non-fwsec input paths).
void fwsec_cleanup(const QString& plainPath);