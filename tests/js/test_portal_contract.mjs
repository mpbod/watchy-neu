import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {test} from 'node:test';

const page = await readFile(
  new URL('../../components/watchy_shell/web/portal.html', import.meta.url),
  'utf8',
);

test('Wi-Fi request payload preserves a saved password unless explicitly changed', () => {
  const begin = page.indexOf('/* WIFI_PAYLOAD_BEGIN */');
  const end = page.indexOf('/* WIFI_PAYLOAD_END */');
  assert.notEqual(begin, -1);
  assert.notEqual(end, -1);
  assert.ok(end > begin);

  const source = page.slice(begin + '/* WIFI_PAYLOAD_BEGIN */'.length, end);
  const wifiRequestPayload = Function(`${source}; return wifiRequestPayload;`)();

  assert.deepEqual(wifiRequestPayload('Home', 'Home', '', false, false), {ssid: 'Home'});
  assert.deepEqual(wifiRequestPayload('Home', 'Other', '', false, false), {ssid: 'Other'});
  assert.deepEqual(
    wifiRequestPayload('Home', 'Other', 'secret123', true, false),
    {ssid: 'Other', password: 'secret123'},
  );
  assert.deepEqual(
    wifiRequestPayload('Home', 'Cafe', '', false, true),
    {ssid: 'Cafe', password: ''},
  );
});
