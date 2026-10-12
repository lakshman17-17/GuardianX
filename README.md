# GuardianX

```
guardianx/
  index.html            the website (open via localhost, see step 6)
  styles.css            design
  app.js                map, ESP32 link, SOS alert, contacts, Firebase logic
  firebase-config.js    <- paste your Firebase web config here
  database/guardianx-database.json   import into Realtime Database
  database/database.rules.json       paste into the Rules tab
  esp32/guardianx_contacts/          Arduino sketch (config.h = Wi-Fi + Firebase URL)
```

## Setup
1. Firebase console: create a project, then Realtime Database -> menu -> Import JSON -> `database/guardianx-database.json`.
2. Realtime Database -> Rules -> paste `database/database.rules.json` -> Publish.
3. Authentication -> Sign-in method -> enable **Anonymous**.
4. Project settings -> Your apps -> Web (</>) -> copy the config -> paste into `firebase-config.js`.
5. Open `esp32/guardianx_contacts/config.h`, set Wi-Fi and the same database URL, then upload the sketch.
6. In this folder run `python -m http.server 8000` and open http://localhost:8000
