# Contract: Kick OAuth 2.1 Authentication Flow

**Version**: 1.0  
**Date**: 2025-12-18  
**Status**: Official API (Documented by Kick)  
**Source**: [Kick Developer Documentation](https://docs.kick.com/getting-started/generating-tokens-oauth2-flow)

---

## Overview

Kick uses OAuth 2.1 with PKCE (Proof Key for Code Exchange) for secure desktop application authentication. This flow allows Chatterino7 to obtain user access tokens for sending chat messages without exposing client secrets.

**Grant Type**: Authorization Code with PKCE  
**Token Lifetime**: ~1 hour (access token), long-lived (refresh token)  
**Required Scope**: `chat:write` (send messages)

---

## Prerequisites

**Kick Developer Application Registration**:
1. Register app at: https://kick.com/dashboard/settings/applications
2. Configure redirect URI: `http://localhost` (with dynamic port)
3. Obtain `client_id` and `client_secret`

Note: For open-source distribution, client secrets should be treated as public (users can view source). PKCE provides security even with public clients.

---

## Flow Steps

### 1. Generate PKCE Parameters

**Code Verifier**: Random 128-character string (Base64 URL-safe)
```cpp
QString generateCodeVerifier() {
    // Generate 96 random bytes (128 chars base64)
    QByteArray randomBytes(96, 0);
    for (int i = 0; i < 96; ++i) {
        randomBytes[i] = QRandomGenerator::global()->bounded(256);
    }
    return randomBytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
```

**Code Challenge**: SHA256 hash of code verifier, Base64 URL-encoded
```cpp
QString generateCodeChallenge(const QString &verifier) {
    QByteArray hash = QCryptographicHash::hash(
        verifier.toUtf8(),
        QCryptographicHash::Sha256
    );
    return hash.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
```

**State Parameter**: Random string for CSRF protection (optional but recommended)
```cpp
QString generateState() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
```

---

### 2. Start Local HTTP Server

**Purpose**: Receive OAuth callback from browser

**Implementation**:
```cpp
QTcpServer *server = new QTcpServer(this);
quint16 port = server->listen(QHostAddress::LocalHost, 0)  // 0 = random port
    ? server->serverPort()
    : 0;

if (port == 0) {
    // Failed to start server
    emit authorizationFailed("Could not start local callback server");
    return;
}

QString redirectUri = QString("http://localhost:%1").arg(port);
```

**Port Range**: Ephemeral ports (49152-65535), let OS assign

---

### 3. Open Authorization URL in System Browser

**URL Format**:
```
https://kick.com/oauth2/authorize?
    response_type=code&
    client_id={CLIENT_ID}&
    redirect_uri={REDIRECT_URI}&
    code_challenge={CODE_CHALLENGE}&
    code_challenge_method=S256&
    scope=chat:write&
    state={STATE}
```

**Parameters**:
- `response_type=code`: Authorization code grant
- `client_id`: Your app's client ID
- `redirect_uri`: `http://localhost:{PORT}` (must match server)
- `code_challenge`: Generated code challenge (Base64 URL-safe)
- `code_challenge_method=S256`: SHA256 hashing method
- `scope=chat:write`: Permission to send chat messages
- `state`: CSRF protection token (validate on callback)

**Example**:
```
https://kick.com/oauth2/authorize?response_type=code&client_id=YOUR_CLIENT_ID&redirect_uri=http://localhost:52341&code_challenge=E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM&code_challenge_method=S256&scope=chat:write&state=8f7d6e5c-4b3a-2918-7c6d-5e4f3a2b1c0d
```

**Open Browser**:
```cpp
QUrl authUrl = buildAuthorizationUrl(codeChallenge, redirectUri, state);
QDesktopServices::openUrl(authUrl);
```

---

### 4. Receive Authorization Code

**Callback URL** (browser redirects here):
```
http://localhost:{PORT}/?code={AUTH_CODE}&state={STATE}
```

**Server Handling**:
```cpp
void KickOAuthFlow::onHttpRequestReceived() {
    QTcpSocket *socket = localServer_->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, [this, socket]() {
        QString request = socket->readAll();
        
        // Parse HTTP GET request
        QUrlQuery query = extractQueryFromRequest(request);
        QString authCode = query.queryItemValue("code");
        QString returnedState = query.queryItemValue("state");
        
        // Validate state (CSRF protection)
        if (returnedState != this->state_) {
            this->sendErrorResponse(socket, "State mismatch");
            return;
        }
        
        // Send success HTML response to browser
        this->sendSuccessResponse(socket);
        socket->close();
        
        // Exchange code for tokens
        this->exchangeCodeForTokens(authCode);
    });
}
```

**Success Response** (HTML to show in browser):
```html
HTTP/1.1 200 OK
Content-Type: text/html

<!DOCTYPE html>
<html>
<head><title>Authorization Successful</title></head>
<body>
    <h1>✓ Successfully connected to Kick!</h1>
    <p>You can close this window and return to Chatterino7.</p>
</body>
</html>
```

---

### 5. Exchange Authorization Code for Tokens

**Endpoint**: `POST https://kick.com/oauth2/token`

**Content-Type**: `application/x-www-form-urlencoded`

**Parameters**:
- `grant_type=authorization_code`
- `client_id`: Your app's client ID
- `client_secret`: Your app's client secret
- `code`: Authorization code from callback
- `redirect_uri`: Must match authorization request
- `code_verifier`: Original code verifier (PKCE proof)

**Request Example** (URL-encoded body):
```
grant_type=authorization_code&
client_id=YOUR_CLIENT_ID&
client_secret=YOUR_CLIENT_SECRET&
code=AUTH_CODE_FROM_CALLBACK&
redirect_uri=http://localhost:52341&
code_verifier=ORIGINAL_CODE_VERIFIER
```

**Qt Implementation**:
```cpp
void KickOAuthFlow::exchangeCodeForTokens(const QString &authCode) {
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    
    QUrlQuery postData;
    postData.addQueryItem("grant_type", "authorization_code");
    postData.addQueryItem("client_id", CLIENT_ID);
    postData.addQueryItem("client_secret", CLIENT_SECRET);
    postData.addQueryItem("code", authCode);
    postData.addQueryItem("redirect_uri", this->redirectUri_);
    postData.addQueryItem("code_verifier", this->codeVerifier_);
    
    QNetworkRequest request(QUrl("https://kick.com/oauth2/token"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    
    QNetworkReply *reply = manager->post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
    
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        this->handleTokenResponse(reply);
        reply->deleteLater();
    });
}
```

---

### 6. Token Response

**Success Response** (200 OK):
```json
{
  "access_token": "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9...",
  "token_type": "Bearer",
  "expires_in": 3600,
  "refresh_token": "def50200a1b2c3d4e5f6...",
  "scope": "chat:write"
}
```

**Fields**:
- `access_token`: JWT access token (use for API requests)
- `token_type`: Always "Bearer"
- `expires_in`: Seconds until access token expires (typically 3600 = 1 hour)
- `refresh_token`: Long-lived token for obtaining new access tokens
- `scope`: Granted scopes (verify includes "chat:write")

**Parse and Store**:
```cpp
void KickOAuthFlow::handleTokenResponse(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        emit authorizationFailed(reply->errorString());
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QJsonObject obj = doc.object();
    
    QString accessToken = obj["access_token"].toString();
    QString refreshToken = obj["refresh_token"].toString();
    int expiresIn = obj["expires_in"].toInt();
    
    // Create account and store tokens
    auto account = std::make_shared<KickAccount>();
    account->setTokens(accessToken, refreshToken, expiresIn);
    account->saveToSettings();  // Persist tokens
    
    emit authorizationSucceeded(account);
}
```

**Error Response** (400 Bad Request):
```json
{
  "error": "invalid_grant",
  "error_description": "The provided authorization grant is invalid, expired, or revoked"
}
```

---

### 7. Refresh Access Token

**When**: Access token expires (1 hour typically) or returns 401 Unauthorized

**Endpoint**: `POST https://kick.com/oauth2/token`

**Content-Type**: `application/x-www-form-urlencoded`

**Parameters**:
- `grant_type=refresh_token`
- `refresh_token`: Current refresh token
- `client_id`: Your app's client ID
- `client_secret`: Your app's client secret

**Request Example**:
```
grant_type=refresh_token&
refresh_token=CURRENT_REFRESH_TOKEN&
client_id=YOUR_CLIENT_ID&
client_secret=YOUR_CLIENT_SECRET
```

**Success Response** (identical structure to authorization code exchange):
```json
{
  "access_token": "NEW_ACCESS_TOKEN",
  "token_type": "Bearer",
  "expires_in": 3600,
  "refresh_token": "NEW_REFRESH_TOKEN",
  "scope": "chat:write"
}
```

**Implementation**:
```cpp
void KickAccount::refreshAccessToken() {
    // Similar to exchangeCodeForTokens but with grant_type=refresh_token
    QUrlQuery postData;
    postData.addQueryItem("grant_type", "refresh_token");
    postData.addQueryItem("refresh_token", this->refreshToken_);
    postData.addQueryItem("client_id", CLIENT_ID);
    postData.addQueryItem("client_secret", CLIENT_SECRET);
    
    // POST to /oauth2/token
    // On success: update tokens, save to settings, emit tokenRefreshed()
    // On failure: emit tokenRefreshFailed(), force re-login
}
```

---

## Security Considerations

### PKCE Benefits

PKCE protects against:
- Authorization code interception (malicious app on localhost)
- Client secret exposure (not needed in public desktop apps)

Even if attacker intercepts authorization code, they cannot exchange it without the original `code_verifier`.

### State Parameter

Validates callback came from same session (CSRF protection). Always validate `state` matches.

### Token Storage

**Secure Storage**:
- Use OS keychain if available (macOS Keychain, Windows Credential Manager, Linux Secret Service)
- Fallback: Obfuscated storage (base64 + XOR, not cryptographically secure but better than plaintext)
- Never store tokens in git, logs, or user-accessible plain text files

**Qt Implementation** (see research.md Decision #7 for details)

### Client Secret Handling

For open-source apps, client secrets are effectively public (users can view source/binaries). PKCE ensures security even if secret is known. Treat secret as "semi-public" and rely on PKCE for security.

---

## Error Handling

### Common Errors

| Error | Description | Client Action |
|-------|-------------|---------------|
| `invalid_request` | Missing required parameter | Check request format, log error |
| `invalid_client` | Client ID/secret incorrect | Update credentials, show error to user |
| `invalid_grant` | Authorization code expired/invalid | Restart authorization flow |
| `unauthorized_client` | Client not authorized for grant type | Configuration issue, contact Kick support |
| `unsupported_grant_type` | Grant type not supported | Use `authorization_code` or `refresh_token` |
| `invalid_scope` | Requested scope invalid | Use `chat:write` only |

### Network Errors

```cpp
void handleNetworkError(QNetworkReply::NetworkError error) {
    switch (error) {
        case QNetworkReply::TimeoutError:
            // Retry with backoff
            this->retryWithBackoff();
            break;
        case QNetworkReply::ConnectionRefusedError:
            // Network issue or Kick API down
            emit authorizationFailed("Cannot connect to Kick. Check internet connection.");
            break;
        default:
            emit authorizationFailed(QString("Network error: %1").arg(error));
            break;
    }
}
```

---

## Flow Diagram

```
[User clicks "Login with Kick"]
         ↓
[Generate PKCE parameters: verifier, challenge, state]
         ↓
[Start local HTTP server on random port]
         ↓
[Open browser with authorization URL]
         ↓
    [User logs in to Kick in browser]
         ↓
    [User approves permissions]
         ↓
[Browser redirects to http://localhost:{PORT}?code=...&state=...]
         ↓
[Local server receives callback]
         ↓
[Validate state parameter]
         ↓
[Send success HTML to browser]
         ↓
[Exchange authorization code + code verifier for tokens]
         ↓
[POST to /oauth2/token]
         ↓
[Receive access_token, refresh_token]
         ↓
[Create KickAccount, save tokens securely]
         ↓
[Emit authorizationSucceeded(account)]
         ↓
[User can now send messages to Kick]

---

[Access token expires after 1 hour]
         ↓
[Auto-refresh using refresh_token]
         ↓
[POST to /oauth2/token with grant_type=refresh_token]
         ↓
[Receive new tokens]
         ↓
[Update account, save new tokens]
```

---

## Testing Strategy

### Manual Testing

1. Click "Login with Kick" button
2. Verify browser opens with correct URL (inspect state, code_challenge parameters)
3. Log in to Kick, approve permissions
4. Verify callback received (check localhost server logs)
5. Verify success page shown in browser
6. Verify Chatterino7 shows "Logged in as {username}"
7. Send message to Kick channel, verify delivery
8. Wait for token expiry (or mock it), verify auto-refresh works
9. Restart app, verify tokens restored from storage

### Unit Tests

```cpp
TEST(KickOAuthFlow, GenerateCodeVerifier) {
    QString verifier = KickOAuthFlow::generateCodeVerifier();
    EXPECT_EQ(verifier.length(), 128);
    EXPECT_TRUE(isBase64UrlSafe(verifier));
}

TEST(KickOAuthFlow, GenerateCodeChallenge) {
    QString verifier = "test_verifier_128_chars...";
    QString challenge = KickOAuthFlow::generateCodeChallenge(verifier);
    EXPECT_EQ(challenge.length(), 43);  // SHA256 base64 length
}

TEST(KickOAuthFlow, ParseTokenResponse) {
    QString json = R"({"access_token":"abc","refresh_token":"def","expires_in":3600})";
    auto tokens = KickOAuthFlow::parseTokenResponse(json);
    EXPECT_EQ(tokens.accessToken, "abc");
    EXPECT_EQ(tokens.expiresIn, 3600);
}
```

---

## Implementation Checklist

- [ ] Generate PKCE code verifier (128-char random Base64 URL-safe)
- [ ] Generate code challenge (SHA256 of verifier, Base64 URL-safe)
- [ ] Generate state parameter (UUID)
- [ ] Start `QTcpServer` on random ephemeral port
- [ ] Build authorization URL with all parameters
- [ ] Open system browser with `QDesktopServices::openUrl()`
- [ ] Handle HTTP callback, parse query parameters
- [ ] Validate state parameter matches
- [ ] Send success HTML response to browser
- [ ] Exchange authorization code for tokens (POST /oauth2/token)
- [ ] Parse token response (access_token, refresh_token, expires_in)
- [ ] Store tokens securely (OS keychain + fallback)
- [ ] Calculate token expiry time (current time + expires_in)
- [ ] Implement auto-refresh before expiry
- [ ] Handle refresh token flow (grant_type=refresh_token)
- [ ] Implement error handling for all failure modes
- [ ] Add timeout for authorization (5 minutes)
- [ ] Shutdown local server after callback received
- [ ] Test on all platforms (Windows, macOS, Linux)

---

## References

- [Kick OAuth 2.1 Documentation](https://docs.kick.com/getting-started/generating-tokens-oauth2-flow)
- [RFC 8252: OAuth 2.0 for Native Apps](https://tools.ietf.org/html/rfc8252)
- [RFC 7636: Proof Key for Code Exchange (PKCE)](https://tools.ietf.org/html/rfc7636)
- [OAuth 2.1 Draft Specification](https://datatracker.ietf.org/doc/html/draft-ietf-oauth-v2-1-08)

