file(SHA256 "${BINARY_FILE}" _hash)
file(SHA256 "${INPUT_FILE}" _fingerprint)
file(WRITE "${RECEIPT_FILE}" "{\"binary\":\"${_hash}\",\"inputs\":\"${_fingerprint}\"}")
