set -euo pipefail

if [ -n "$AUTH_JSON" ]; then
  if ! printf '%s' "$AUTH_JSON" | jq -e '
    type == "object" and
    (.tokens.access_token? | type == "string" and length > 0) and
    (.OPENAI_API_KEY? == null or .OPENAI_API_KEY == "")
  ' > /dev/null 2>&1; then
    echo "::error::CODEX_AUTH_JSON must contain a ChatGPT subscription login."
    exit 1
  fi
  umask 077
  mkdir -p "$RUNNER_TEMP/codex-home"
  printf '%s' "$AUTH_JSON" > "$RUNNER_TEMP/codex-home/auth.json"
  chmod 600 "$RUNNER_TEMP/codex-home/auth.json"
  {
    echo "home=$RUNNER_TEMP/codex-home"
    echo "have=true"
  } >> "$GITHUB_OUTPUT"
else
  echo "have=false" >> "$GITHUB_OUTPUT"
  echo "CODEX_AUTH_JSON is not set, so Codex did not run." >> "$GITHUB_STEP_SUMMARY"
fi
