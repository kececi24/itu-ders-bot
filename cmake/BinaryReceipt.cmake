file(SHA256 "${BINARY_FILE}" _hash)
file(WRITE "${RECEIPT_FILE}" "${_hash}")
