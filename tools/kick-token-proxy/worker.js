// Holds the Kick client secret so it never ships in Chatterino binaries.
// Only forwards code->token and refresh requests for this one client.
const KICK_TOKEN_URL = 'https://id.kick.com/oauth/token';
const REDIRECT_URI = 'http://localhost:52847/callback';

function error(status, code, description) {
    return Response.json(
        { error: code, error_description: description },
        { status, headers: { 'Cache-Control': 'no-store' } },
    );
}

export default {
    async fetch(request, env) {
        const url = new URL(request.url);
        if (url.pathname !== '/token') {
            return error(404, 'not_found', 'Unknown path');
        }
        if (request.method !== 'POST') {
            return error(405, 'invalid_request', 'POST only');
        }
        const body = await request.text();
        if (body.length > 8192) {
            return error(413, 'invalid_request', 'Body too large');
        }

        const form = new URLSearchParams(body);
        if (form.get('client_id') !== env.KICK_CLIENT_ID) {
            return error(400, 'invalid_client', 'Unknown client_id');
        }

        const out = new URLSearchParams({
            client_id: env.KICK_CLIENT_ID,
            client_secret: env.KICK_CLIENT_SECRET,
        });
        const grant = form.get('grant_type');
        let fields;
        if (grant === 'authorization_code') {
            if (form.get('redirect_uri') !== REDIRECT_URI) {
                return error(400, 'invalid_request', 'Unexpected redirect_uri');
            }
            fields = ['code', 'code_verifier', 'redirect_uri'];
        } else if (grant === 'refresh_token') {
            fields = ['refresh_token'];
        } else {
            return error(400, 'unsupported_grant_type', 'Unsupported grant_type');
        }
        out.set('grant_type', grant);
        for (const field of fields) {
            const value = form.get(field);
            if (!value) {
                return error(400, 'invalid_request', `Missing ${field}`);
            }
            out.set(field, value);
        }

        const upstream = await fetch(KICK_TOKEN_URL, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
                Accept: 'application/json',
            },
            body: out,
        });
        return new Response(upstream.body, {
            status: upstream.status,
            headers: {
                'Content-Type':
                    upstream.headers.get('Content-Type') ?? 'application/json',
                'Cache-Control': 'no-store',
            },
        });
    },
};
