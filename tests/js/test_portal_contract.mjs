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
  assert.deepEqual(
    wifiRequestPayload('Home', 'Home', '', true, false),
    {ssid: 'Home'},
  );
});

test('timezone control renders exactly the settings-policy offsets', () => {
  const match = page.match(/const TIMEZONE_OFFSETS = Object\.freeze\(\[([^\]]+)\]\);/);
  assert.ok(match, 'portal exposes its canonical timezone offsets');
  const offsets = match[1].split(',').map((value) => Number(value.trim()));
  assert.deepEqual(offsets, [
    -720, -660, -600, -570, -540, -480, -420, -360, -300, -240,
    -210, -180, -120, -60, 0, 60, 120, 180, 210, 240, 270, 300,
    330, 345, 360, 390, 420, 480, 525, 540, 570, 600, 630, 660,
    720, 765, 780, 840,
  ]);
});
