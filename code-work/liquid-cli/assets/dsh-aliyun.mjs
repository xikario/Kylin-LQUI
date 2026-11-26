import { Context } from '@deepseek-ai/cordis';
import LocalCredentials from '@deepseek-ai/dsh-credentials-local';
import { withFileLock, writeFileAtomic } from '@deepseek-ai/dsh-atomic-write';
import { parseDocument, isSeq, isMap } from 'yaml';
import { mkdir, readFile } from 'node:fs/promises';
import { homedir } from 'node:os';
import { join, resolve } from 'node:path';

const routes = {
    plan: { name: '阿里云 Token Plan', provider: 'aliyun-token-plan', ref: 'ALIYUN_TOKEN_PLAN_API_KEY', url: 'https://token-plan.cn-beijing.maas.aliyuncs.com/compatible-mode/v1', defaultModel: 'deepseek-v4.1-flash' },
    general: { name: '百炼通用 API（北京）', provider: 'aliyun-dashscope', ref: 'LQUI_ALIYUN_DASHSCOPE_API_KEY', url: 'https://dashscope.aliyuncs.com/compatible-mode/v1', defaultModel: 'deepseek-v4.1-flash' },
    official: { name: 'DeepSeek 官方', provider: 'deepseek-official', ref: 'DEEPSEEK_API_KEY', url: 'https://api.deepseek.com', defaultModel: 'deepseek-flash' },
};
const profileDir = join(resolve(process.env.DSH_HOME || join(homedir(), '.dsh')), 'profiles', 'acp');
const patchPath = join(profileDir, 'cordis.patch.yml');
const root = new Context();

async function profile() {
    let source;
    try { source = await readFile(patchPath, 'utf8'); }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
    const doc = parseDocument(source || '[]');
    if (doc.errors.length || !isSeq(doc.contents)) throw new Error('invalid profile');
    return doc;
}

function entry(doc, id) {
    let node = doc.contents.items.find(item => isMap(item) && item.get('id') === id);
    if (!node) {
        node = doc.createNode({ id });
        doc.contents.add(node);
    }
    return node;
}

function validModel(id) {
    return typeof id === 'string' && id.length > 0 && id.length <= 256 && !/[\s\u0000-\u001f\u007f]/u.test(id);
}

function chatModels(entries) {
    if (!Array.isArray(entries)) return [];
    return [...new Set(entries.filter(m => {
        if (!validModel(m?.id)) return false;
        if (Array.isArray(m.output_modalities) && !m.output_modalities.includes('text')) return false;
        // Shared gateways also list media generation, speech and retrieval endpoints.
        return !/(?:^wan\d|^happyhorse|^qwen-image|^qwen-audio|(?:^|[-_/])(?:tts|asr|embedding|rerank(?:er)?|realtime)(?:[-_/]|$))/i.test(m.id);
    }).map(m => m.id))];
}

function configureProvider(doc, route, model) {
    if (route === routes.official) {
        // The built-in Harness adapter uses the official Anthropic-compatible transport.
        const native = entry(doc, 'llm-deepseek');
        native.set('disabled', false);
        native.setIn(['config', 'apiKeyEnv'], route.ref);
        native.setIn(['config', 'baseURL'], route.url + '/anthropic');
    } else {
        const llm = entry(doc, 'llm-pi-ai');
        const path = ['config', 'providers', route.provider];
        const oldUrl = llm.getIn([...path, 'baseURL']);
        if (oldUrl && oldUrl !== route.url) llm.deleteIn([...path, 'models']);
        for (const [field, value] of Object.entries({
            api: 'openai-completions', displayName: route.name, baseURL: route.url, apiKeyEnv: route.ref,
        })) llm.setIn([...path, field], value);
        llm.setIn([...path, 'compat', 'supportsDeveloperRole'], false);
        llm.setIn([...path, 'compat', 'maxTokensField'], 'max_tokens');
        const models = llm.getIn([...path, 'models'], true);
        if (models && !isSeq(models)) throw new Error('invalid models');
        if (!models || !models.items.some(item => isMap(item) && item.get('id') === model)) {
            const item = { id: model, name: model, input: ['text'] };
            if (models) models.add(doc.createNode(item));
            else llm.setIn([...path, 'models'], [item]);
        }
    }
    entry(doc, 'credentials').set('disabled', false);
    for (const id of ['acp', 'agent-default-model']) {
        const node = entry(doc, id);
        node.setIn(['config', 'provider'], route.provider);
        node.setIn(['config', 'model'], model);
    }
}

async function save(route, model, key, credentials) {
    await mkdir(profileDir, { recursive: true, mode: 0o700 });
    return withFileLock(join(profileDir, 'package.json'), async () => {
        const doc = await profile();
        configureProvider(doc, route, model);
        const next = String(doc);
        if (key) await credentials.set(route.ref, key);
        else if (!(await credentials.describe(route.ref)).configured) return { state: 'empty-input' };
        try { await writeFileAtomic(patchPath, next, { mode: 0o600, dirMode: 0o700 }); }
        catch { return { state: key ? 'key-only' : 'failed' }; }
        return { state: 'saved', model, acpModel: JSON.stringify([route.provider, model]) };
    });
}

async function status(credentials) {
    const doc = (await profile()).toJS();
    const acp = doc.find(item => item?.id === 'acp')?.config;
    const providers = doc.find(item => item?.id === 'llm-pi-ai')?.config?.providers || {};
    const selectedUrl = providers[acp?.provider]?.baseURL;
    const active = acp?.provider === routes.official.provider ? 'official'
        : Object.keys(routes).find(k => routes[k].url === selectedUrl) || '';
    const options = {};
    for (const [k, r] of Object.entries(routes)) {
        const config = Object.values(providers).find(p => p?.baseURL === r.url);
        const current = k === active && validModel(acp?.model) ? acp.model : r.defaultModel;
        options[k] = { configured: (await credentials.describe(r.ref)).configured, current,
            models: [...new Set([current, ...chatModels(config?.models)])] };
    }
    return { state: 'status', active, options };
}

async function run() {
    await root.plugin(LocalCredentials, { watch: false });
    const credentials = root.get('credentials');
    const [, action, type, model] = process.argv;
    if (action === 'status') return status(credentials);
    if (!Object.hasOwn(routes,type) || !['models','save','test'].includes(action)) return { state: 'failed' };
    const route = routes[type];
    const chunks = [];
    for await (const chunk of process.stdin) chunks.push(chunk);
    const input = Buffer.concat(chunks).toString('utf8').trim();
    if (input && /\s/.test(input)) return { state: 'invalid' };
    if (type === 'official' && input.startsWith('sk-sp-')) return { state: 'wrong-key' };
    if (action !== 'models' && !validModel(model)) return { state: 'choose-model' };
    if (action === 'save') return save(route, model, input, credentials);
    const key = input || (await credentials.resolve(route.ref))?.value;
    if (!key) return { state: 'empty-input' };
    const listing = action === 'models';
    const options = { method: listing ? 'GET' : 'POST', redirect: 'error', signal: AbortSignal.timeout(25000),
        headers: { Authorization: 'Bearer ' + key } };
    if (!listing) {
        options.headers['Content-Type'] = 'application/json';
        options.body = JSON.stringify({ model, stream: false, max_tokens: 64,
            ...(type === 'official' ? { thinking: { type: 'disabled' } } : {}),
            messages: [{ role: 'user', content: 'Reply OK.' }] });
    }
    const response = await fetch(route.url + (listing ? '/models' : '/chat/completions'), options);
    const body = await response.json().catch(() => ({}));
    if (response.ok) {
        if (!listing) return { state: body.choices?.length ? 'ok' : 'bad-response', model };
        const models = chatModels(body.data);
        return { state: models.length ? 'models' : 'empty-list', models };
    }
    const code = body.error?.code || body.code;
    if (response.status === 429) return { state: code === 'insufficient_quota' ? 'quota' : 'rate' };
    if (response.status === 402) return { state: 'balance' };
    if (response.status === 403) return { state: 'denied' };
    if (response.status === 401) return { state: 'auth' };
    if (response.status === 404) return { state: 'model' };
    return { state: 'http-' + response.status };
}

try { console.log(JSON.stringify(await run())); }
catch (error) { console.log(JSON.stringify({ state: error?.name === 'TimeoutError' ? 'timeout' : 'failed' })); }
finally { await root.fiber.dispose(); }
