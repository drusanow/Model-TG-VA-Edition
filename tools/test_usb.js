// Checks the flasher's sending over USB (flasher/usb.js, from Modded-Cycles) against a stand-in
// Model:Cycles that speaks the Transfer update protocol: the page's .syx passes usb.js's checks, and
// every block reaches the machine intact (its CRC, length and offset), in order, and nothing else.
//   python3 build.py --stock STOCK.syx --flasher flasher/model-tg.json
//   node tools/test_usb.js STOCK.syx
'use strict';
var fs = require('fs'), path = require('path');
var here = path.join(__dirname, '..', 'flasher');
require(path.join(here, 'elektron.js'));
require(path.join(here, 'patch.js'));
var F = require(path.join(here, 'usb.js'));

var stock = process.argv[2];
if (!stock) { console.error('usage: node tools/test_usb.js STOCK.syx'); process.exit(2); }
var patch = JSON.parse(fs.readFileSync(path.join(here, 'model-tg.json'), 'utf8'));
var syx = globalThis.ModelTG.flash(new Uint8Array(fs.readFileSync(stock)), patch, function () {});

function check(ok, what) {
  if (!ok) { console.log('FAIL ' + what); process.exit(1); }
  console.log('ok   ' + what);
}

var v = F.verify(syx);
check(v.product === 0x11 && v.name === 'Model:Cycles', 'the built .syx passes usb.js verify (' + v.count + ' packets)');

// A stand-in Model:Cycles: answers ping, version, upgrade start and blocks as the machine does.
function cycles(id) {
  var listeners = [], got = new Uint8Array(0), size = -1, bad = null;
  function reply(req, body) {
    var p = new Uint8Array(5 + body.length);
    p[0] = 0; p[1] = 0; p[2] = req[0]; p[3] = req[1]; p[4] = req[4] | 0x80;
    p.set(body, 5);
    var msg = F.xferFrame(p);
    setTimeout(function () { listeners.forEach(function (l) { l({ data: msg }); }); }, 1);
  }
  var str = function (s) { return Array.from(s).map(function (c) { return c.charCodeAt(0); }).concat([0]); };
  return {
    input: { addEventListener: function (t, l) { listeners.push(l); }, removeEventListener: function () {} },
    output: { send: function (m) {
      var q = F.xferParse(m), cmd = q[4];
      if (cmd === 0x01) reply(q, [id, 0].concat(str('Model:Cycles')));
      else if (cmd === 0x02) reply(q, [0, 0, 0, 0, 0].concat(str('1.13')));
      else if (cmd === 0x50) { size = q[5] | q[6] << 8 | q[7] << 16 | q[8] << 24; reply(q, [0]); }
      else if (cmd === 0x51) {
        var be = function (o) { return ((q[o] << 24) | (q[o + 1] << 16) | (q[o + 2] << 8) | q[o + 3]) >>> 0; };
        var crc = be(5), len = be(9), off = be(13), blk = q.subarray(17, 17 + len);
        if (off !== got.length) bad = bad || 'block at ' + off + ', expected ' + got.length;
        if (F.crc32(blk, 0xffffffff) !== crc) bad = bad || 'bad CRC at ' + off;
        var n = new Uint8Array(got.length + len); n.set(got); n.set(blk, got.length); got = n;
        reply(q, [0, 0, 0, 0, got.length >= size ? 1 : 0]);
      }
    } },
    result: function () { return { got: got, size: size, bad: bad }; },
  };
}

(async function () {
  var dev = cycles(27);
  var s = await F.xferOpen(dev.input, dev.output);
  var info = await F.xferIdentify(s);
  check(info.device === 'Model:Cycles' && info.version === '1.13', 'identify: ' + info.device + ' OS ' + info.version);
  var res = await F.upgradeFast(s, syx, { restMs: 0 });
  s.close();
  var r = dev.result();
  check(!r.bad, 'every block arrives in order with its CRC' + (r.bad ? ' (' + r.bad + ')' : ''));
  check(r.size === syx.length && globalThis.Elektron.eq(r.got, syx),
    'the machine received the whole .syx (' + res.blocks + ' blocks, ' + syx.length + ' B)');

  var smp = cycles(25);
  s = await F.xferOpen(smp.input, smp.output);
  info = await F.xferIdentify(s);
  s.close();
  check(info.device === 'Model:Samples', 'a Cycles set to SMP answers as a Model:Samples (the page refuses it)');
  console.log('ALL OK');
})().catch(function (e) { console.log('FAIL ' + (e.stack || e)); process.exit(1); });
