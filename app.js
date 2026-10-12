/*
  GUARDIANX — WEBSITE APP.JS
  Firebase Realtime Database
  ESP32 + SIM900A Dashboard
*/

const $ = id => document.getElementById(id);
const log = message => {
  $('msg').textContent = message;
};

let mk = null;
let tile = null;
let mode = '';
let last = 0;
let alerting = false;
let responded = false;
let wr = null;
let ip = '';
let poll = null;
let sim = null;
let lastGeo = 0;
let hist = [];
let synced = false;
let db = null;
let fbErr = 'Firebase is not ready yet.';
let ownerUid = null;

const DEVICE_KEY =
  typeof DEVICE_ID !== 'undefined'
    ? DEVICE_ID
    : 'guardianx001';

try {
  hist = JSON.parse(localStorage.gxh || '[]');
} catch (e) {
  hist = [];
}

/* ========== MAP ========== */

if (!window.L) {
  const fake = () => ({
    addTo() { return this; },
    on() { return this; },
    setView() { return this; },
    addLatLng() {},
    setLatLng() {},
    setIcon() {},
    removeLayer() {},
    panTo() {},
    invalidateSize() {},
    getBounds() {
      return { contains: () => true };
    }
  });

  window.L = {
    map: fake,
    polyline: fake,
    tileLayer: fake,
    marker: fake,
    divIcon: () => ({})
  };

  $('map').textContent =
    'Map library could not load. Check internet.';
}

const map = L.map('map').setView(
  [20.59, 78.96],
  5
);

map.attributionControl?.setPosition('topright');

const trail = L.polyline([], {
  color: '#22d3ee',
  weight: 3
}).addTo(map);

function tiles() {
  if (tile) map.removeLayer(tile);

  tile = L.tileLayer(
    'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
    {
      maxZoom: 19,
      attribution: '© OpenStreetMap contributors'
    }
  ).addTo(map);
}

tiles();

addEventListener('load', () => {
  setTimeout(() => map.invalidateSize(), 300);
});

$('theme').onclick = () => {
  const r = document.documentElement;

  r.dataset.theme =
    r.dataset.theme === 'dark'
      ? 'light'
      : 'dark';
};

const icon = sos => L.divIcon({
  className: '',
  html: `<div class="pin ${sos ? 'sos' : ''}"></div>`,
  iconSize: [22, 22]
});

/* ========== STATUS ========== */

function pill(status, text) {
  const p = $('pill');
  p.className = 'pill ' + status;
  p.textContent = text;
}

function renderHist() {
  const ul = $('hist');
  ul.textContent = '';

  const items = hist.length
    ? hist
    : [{ t: 'No alerts yet' }];

  items.slice(0, 8).forEach(h => {
    const li = document.createElement('li');

    li.textContent = h.lat
      ? `${h.t} · ${h.lat.toFixed(4)}, ${h.lng.toFixed(4)}`
      : h.t;

    ul.appendChild(li);
  });
}

renderHist();

/* ========== FIREBASE PATHS ========== */

function contactsRef() {
  if (!db) {
    throw new Error('Firebase not connected');
  }

  const user = firebase.auth().currentUser;

  if (!user) {
    throw new Error('Firebase authentication not ready');
  }

  return db.ref(
    'users/' + user.uid + '/contacts'
  );
}

function deviceRef(path) {
  if (!db) {
    throw new Error('Firebase not connected');
  }

  return db.ref(
    'devices/' + DEVICE_KEY + '/' + path
  );
}

/* ========== ALERT ========== */

function raise(lat, lng) {
  $('alert').classList.add('on');

  $('at').textContent =
    '🚨 SOS RECEIVED — help needed';

  if (mk) mk.setIcon(icon(true));

  // Device alerts should be written by the ESP32.
  // Avoid duplicate writes from the website.

  hist.unshift({
    t: new Date().toLocaleString(),
    lat,
    lng
  });

  hist = hist.slice(0, 20);

  try {
    localStorage.gxh = JSON.stringify(hist);
  } catch (e) {}

  renderHist();

  try {
    const ctx = new AudioContext();
    const oscillator = ctx.createOscillator();

    oscillator.type = 'square';
    oscillator.frequency.value = 880;
    oscillator.connect(ctx.destination);
    oscillator.start();
    oscillator.stop(ctx.currentTime + 0.5);
  } catch (e) {}
}

function clearAlert(text) {
  $('alert').classList.remove('on');

  $('at').textContent = text || 'All clear';

  if (mk) mk.setIcon(icon(false));
}

/* ========== DEVICE DATA ========== */

function updateDevice(d) {
  last = Date.now();

  pill('on', 'Connected · ' + mode);

  $('lu').textContent =
    new Date().toLocaleTimeString();

  const lat = Number(d.lat ?? d.latitude);
  const lng = Number(d.lng ?? d.longitude);

  const fix =
    Number.isFinite(lat) &&
    Number.isFinite(lng) &&
    (lat !== 0 || lng !== 0);

  if (fix) {
    const position = [lat, lng];

    if (!mk) {
      mk = L.marker(position, {
        icon: icon(false)
      }).addTo(map);

      map.setView(position, 16);
    } else {
      mk.setLatLng(position);

      if (!map.getBounds().contains(position)) {
        map.panTo(position);
      }
    }

    trail.addLatLng(position);

    $('co').textContent =
      lat.toFixed(5) + ', ' + lng.toFixed(5);

    geo(lat, lng);
  } else {
    $('co').textContent =
      'Waiting for GPS fix…';
  }

  const battery = d.bat ?? d.batteryPercent;

  if (battery != null) {
    const b = Math.max(
      0,
      Math.min(100, Number(battery))
    );

    $('bat').textContent = b + '%';
    $('bf').style.width = b + '%';
  }

  if (d.gsm != null) {
    const n = Math.ceil(
      Math.min(31, Number(d.gsm)) / 8
    );

    [...$('sig').children].forEach((el, index) => {
      el.classList.toggle('on', index < n);
    });
  }

  const sos = Boolean(d.sos ?? d.sosActive);

  if (sos && !alerting) {
    alerting = true;
    responded = false;
    raise(fix ? lat : 0, fix ? lng : 0);
  } else if (!sos && alerting) {
    alerting = false;

    if (!responded) clearAlert();
  }
}

/* ========== ADDRESS ========== */

async function geo(lat, lng) {
  if (Date.now() - lastGeo < 15000) return;

  lastGeo = Date.now();

  try {
    const url =
      'https://nominatim.openstreetmap.org/reverse' +
      '?format=json&lat=' + encodeURIComponent(lat) +
      '&lon=' + encodeURIComponent(lng);

    const response = await fetch(url);
    const data = await response.json();

    $('ad').textContent =
      '📍 ' + (data.display_name || 'Address not found');
  } catch (e) {
    $('ad').textContent =
      'Address lookup unavailable';
  }
}

setInterval(() => {
  if (mode && Date.now() - last > 15000) {
    pill('warn', 'No data · ' + mode);
  }
}, 3000);

/* ========== CONNECTION CONTROL ========== */

function stop() {
  if (poll) clearInterval(poll);
  if (sim) clearInterval(sim);

  poll = null;
  sim = null;
  ip = '';
  wr = null;
  synced = false;
}

/* ========== USB ========== */

$('usb').onclick = async () => {
  if (!('serial' in navigator)) {
    log('USB requires Chrome or Edge desktop.');
    return;
  }

  try {
    const port = await navigator.serial.requestPort();

    await port.open({
      baudRate: 115200
    });

    stop();

    mode = 'USB';
    pill('on', 'Connected · USB');
    log('Reading ESP32…');

    wr = port.writable.getWriter();

    const decoder = new TextDecoderStream();

    port.readable.pipeTo(decoder.writable);

    const reader = decoder.readable.getReader();

    let buffer = '';

    for (;;) {
      const { value, done } = await reader.read();

      if (done) break;

      buffer += value;

      let index;

      while ((index = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, index).trim();

        buffer = buffer.slice(index + 1);

        try {
          updateDevice(JSON.parse(line));
        } catch (e) {}
      }
    }
  } catch (e) {
    log('USB connection failed: ' + e.message);
  }
};

/* ========== LOCAL ESP32 WI-FI ========== */

$('wifi').onclick = () => {
  const value = $('ip').value.trim();

  if (!value) {
    log('Enter ESP32 IP address.');
    return;
  }

  stop();

  ip = value;
  mode = 'Wi-Fi';

  log('Connecting to ESP32: ' + ip);

  poll = setInterval(async () => {
    try {
      const response = await fetch(
        'http://' + ip + '/data'
      );

      if (!response.ok) {
        throw new Error('HTTP ' + response.status);
      }

      updateDevice(await response.json());
    } catch (e) {
      log(
        'Cannot reach ESP32. Check network and browser restrictions.'
      );
    }
  }, 2000);
};

/* ========== DETECT MY LOCATION ========== */

$('me').onclick = () => {
  if (!navigator.geolocation) {
    log('Geolocation unsupported');
    return;
  }

  navigator.geolocation.getCurrentPosition(
    position => {
      stop();
      mode = 'Browser GPS';

      updateDevice({
        lat: position.coords.latitude,
        lng: position.coords.longitude
      });

      log('Showing your current device location.');
    },
    error => {
      log('Location error: ' + error.message);
    },
    {
      enableHighAccuracy: true,
      timeout: 15000
    }
  );
};

/* ========== DEMO ========== */

$('demo').onclick = () => {
  stop();

  mode = 'Demo';

  let lat = 20.5937;
  let lng = 78.9629;
  let count = 0;
  let battery = 92;

  log('Demo running. SOS begins after about 12 seconds.');

  sim = setInterval(() => {
    count++;

    lat += (Math.random() - 0.4) * 0.0004;
    lng += (Math.random() - 0.4) * 0.0004;

    battery = Math.max(5, battery - 0.2);

    updateDevice({
      lat,
      lng,
      bat: Math.round(battery),
      gsm: 20,
      sos: count >= 6 && count <= 30 ? 1 : 0
    });
  }, 2000);
};

/* ========== RESPONSE BUTTON ========== */

$('resp').onclick = async () => {
  let commandSent = false;

  try {
    if (wr) {
      await wr.write(
        new TextEncoder().encode('RESPOND\n')
      );
      commandSent = true;
    } else if (ip) {
      const response = await fetch(
        'http://' + ip + '/respond'
      );

      if (!response.ok) {
        throw new Error('HTTP ' + response.status);
      }

      commandSent = true;
    } else if (db) {
      const snap = await deviceRef('status/alertId').get();
      const alertId = snap.val();

      if (alertId) {
        await deviceRef(
          'alerts/' + alertId + '/ack'
        ).update({
          acknowledged: true,
          acknowledgedAt:
            firebase.database.ServerValue.TIMESTAMP
        });

        commandSent = true;
      } else {
        throw new Error('No active device alert found');
      }
    } else {
      throw new Error('No connection available');
    }

    if (commandSent) {
      responded = true;

      $('alert').classList.remove('on');

      $('at').textContent =
        '✅ Response request sent';

      if (mk) mk.setIcon(icon(false));

      log('Response submitted. Waiting for ESP32 acknowledgement.');
    }
  } catch (e) {
    log('Respond failed: ' + e.message);
  }
};

/* ========== EMERGENCY CONTACTS ========== */

const cn = [...document.querySelectorAll('.cn')];

const cm = message => {
  $('cmsg').textContent = message;
};

function normalizeContacts(value) {
  if (!value) return [];

  if (Array.isArray(value)) {
    return value.slice(0, 4);
  }

  return [
    value.slot1 || value['0'] || '',
    value.slot2 || value['1'] || '',
    value.slot3 || value['2'] || '',
    value.slot4 || value['3'] || ''
  ];
}

function fillContacts(list) {
  cn.forEach((input, index) => {
    input.value = list[index] || '';
  });

  cm(
    list.some(Boolean)
      ? 'Contacts loaded from Firebase ✅'
      : 'No contacts saved yet.'
  );
}

async function sendContacts(list) {
  const message = list.join(',');

  try {
    if (wr) {
      await wr.write(
        new TextEncoder().encode(
          'CONTACTS:' + message + '\n'
        )
      );

      cm('Contacts sent over USB.');
    } else if (ip) {
      const response = await fetch(
        'http://' + ip +
        '/contacts?n=' +
        encodeURIComponent(message)
      );

      if (!response.ok) {
        throw new Error('HTTP ' + response.status);
      }

      cm('Contacts request sent over Wi-Fi.');
    }
  } catch (e) {
    cm('Sending to ESP32 failed: ' + e.message);
  }
}

async function syncDb(push = false) {
  if (!db) {
    cm(fbErr);
    return;
  }

  try {
    const snapshot = await contactsRef().get();

    const list = normalizeContacts(
      snapshot.val()
    );

    fillContacts(list);

    if (push && list.some(Boolean)) {
      await sendContacts(list);
    }
  } catch (e) {
    cm('Could not read database: ' + e.message);
  }
}

$('cload').onclick = () => {
  syncDb(false);
};

/* ========== SAVE FOUR CONTACTS ========== */

$('csave').onclick = async () => {
  const list = cn.map(input =>
    input.value.replace(/[\s-]/g, '')
  );

  const phonePattern = /^\+?\d{10,15}$/;

  if (list.length !== 4 ||
      list.some(number => !phonePattern.test(number))) {
    cm('Enter 4 valid phone numbers with country code.');
    return;
  }

  if (new Set(list).size !== 4) {
    cm('All 4 phone numbers must be different.');
    return;
  }

  if (!db) {
    cm(fbErr);
    return;
  }

  try {
    // Save under the currently authenticated website user.
    await contactsRef().set(list);

    cm('Saved to Firebase ✅');

    // Saving does not prove the ESP32 has received them.
  } catch (e) {
    cm('Could not save: ' + e.message);
  }
};

/* ========== FIREBASE INITIALIZATION ========== */

(function initializeFirebase() {
  const config =
    typeof FIREBASE_CONFIG !== 'undefined'
      ? FIREBASE_CONFIG
      : typeof firebaseConfig !== 'undefined'
        ? firebaseConfig
        : null;

  function bad(message) {
    fbErr = message;
    cm(message);
  }

  if (!window.firebase) {
    return bad('Firebase library not loaded.');
  }

  if (!config) {
    return bad('firebase-config.js missing or invalid.');
  }

  if (!config.apiKey || !config.databaseURL) {
    return bad('Firebase API key or database URL missing.');
  }

  try {
    if (!firebase.apps.length) {
      firebase.initializeApp(config);
    }

    db = firebase.database();
  } catch (e) {
    return bad('Firebase setup failed: ' + e.message);
  }

  firebase.auth().signInAnonymously()
    .then(userCredential => {
      ownerUid = userCredential.user.uid;

      console.log('GuardianX OWNER_UID:', ownerUid);

      cm('Firebase connected ✅');

      // Contact listener
      contactsRef().on(
        'value',
        snapshot => {
          const list = normalizeContacts(
            snapshot.val()
          );

          fillContacts(list);

          if ((wr || ip) && list.some(Boolean)) {
            sendContacts(list);
          }
        },
        error => {
          cm(
            'Firebase contacts read failed: ' +
            error.message
          );
        }
      );

      // Live device data listener
      deviceRef('live').on(
        'value',
        snapshot => {
          const data = snapshot.val();

          if (data && !wr && !ip) {
            mode = 'Firebase';
            updateDevice(data);
          }
        },
        error => {
          console.warn(
            'Live device read:',
            error.message
          );
        }
      );
    })
    .catch(error => {
      bad(
        'Firebase sign-in failed: ' +
        error.message
      );
    });
})();