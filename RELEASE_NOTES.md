# v1.2.8 — Facebook Instant Games workflow

## Added
- Upload Instant Games bundles to Meta Web Hosting directly from the launcher (upload only; **never publishes to Production**).
- Upload an existing ZIP or create a ZIP from the selected Cocos build directory without freezing the UI.
- Normalize upload archives so the content is nested under `fb-instant-games/`, including `index.html` and `fbapp-config.json`.
- Fetch a Meta App Access Token using App ID + App Secret; App Secret is not persisted. App Access Tokens may not have Web Hosting upload rights.
- Store upload tokens per game in Windows Credential Manager; show/hide saved tokens.
- Open the selected game's Meta Developer Dashboard from the launcher.
- Dependency checks/install helpers for Node.js, npm, http-server and mkcert.
- Use per-game certificates where provided, otherwise shared localhost certificates; offer mkcert creation.

## Improved
- Detect an occupied HTTPS port before launch, show diagnostics, and wait for HTTPS readiness.
- Launch Node.js directly so normal Stop/Exit also stops the HTTP server process.
- Double-click a build-directory cell to open it in Explorer.
- Show Graph API error code/subcode/trace ID for failed uploads.
- Keep the upload dialog responsive during ZIP creation/normalization and unlock it after errors.

## Notes
- Production publishing remains manual in Meta Developer Dashboard.
- For credential safety, ZIP packaging excludes localhost private keys and certificate files.
- Some functionality requires Node.js, npm, http-server, mkcert and/or Windows PowerShell.
