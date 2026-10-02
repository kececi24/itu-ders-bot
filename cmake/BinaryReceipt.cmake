file(SHA256 "${BINARY_FILE}" _hash)
# file(WRITE) uses native text newlines on Windows. Hash the same normalized
# text as the generated header and finalizer, not the CRLF bytes on disk.
file(READ "${INPUT_FILE}" _inputs)
string(SHA256 _fingerprint "${_inputs}")
file(WRITE "${RECEIPT_FILE}" "{\"binary\":\"${_hash}\",\"inputs\":\"${_fingerprint}\"}")
