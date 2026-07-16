# 🚨 सचेतक​ (Sachetak) - Smart Safety Helmet for Construction Workers

<img width="830" height="150" alt="github" src="https://github.com/user-attachments/assets/9e7a2f58-95bd-42a5-918f-57537aa62fdd" />
<br>
सचेतक is a smart safety helmet designed for industrial and construction workers. Standard hard hats only protect workers after an object falls on their heads. सचेतक actively monitors the environment and the worker’s vitals to protect them from invisible hazards before they start causing harm and reports it to the supervisor team.

Developed as our first-ever hardware project for the Aavishkar 2026 hackathon. 🛠️

---

## Key Features

*   **Environmental & Health Monitoring:** Tracks dangerous gas leaks, high ambient temperatures, air quality, live heart rate (BPM), and sudden high-impact falls.
*   **Off-Grid Communication (ESP-NOW):** Utilizes the ESP32's ESP-NOW protocol for peer-to-peer mesh communication. Helmets can talk to each other and relay data back to the supervisor even in deep mines or remote areas with absolutely no internet or cellular coverage.
*   **Supervisor Dashboard:** A web-based supervisor dashboard that displays real-time vitals for every worker on-site.
*   **Real-Time Emergency Localization:** Includes an "Alerts" section in the dashboard that immediately shows the vulnerable worker's exact coordinates using a GPS module mapped onto OpenStreetMap.
*   **Peer-to-Peer Safety Nets:** Triggers localized alerts to nearby coworkers instantly during an emergency (like a toxic gas leak) so teammates can respond in seconds.

---
<img width="1200" height="627" alt="image" src="https://github.com/user-attachments/assets/c5519ad3-964c-43a6-b511-afdde1537279" />

---

## 🛠️ Tech Stack & Hardware Components

### **Hardware Components**
*   **Microcontroller:** ESP32 (chosen for its processing power and built-in Wi-Fi/Bluetooth capabilities)
*   **Communication:** ESP-NOW Protocol *(Future Roadmap: LoRa modules for long-range scaling)*
*   **Sensors & Modules:**
    *   Pulse Oximeter / Heart Rate Sensor (MAX30102)
    *   Gas & Air Quality Sensor (MQ-5 and MQ-135)
    *   Temperature & Humidity Sensor (DHT11)
    *   Accelerometer / Gyroscope (Fall detection - MPU6050)
    *   Barometric pressure and altitude detection (BMP180)
    *   GPS Module (Neo-6M)
    *   Active buzzer
    *   0.96 inch OLED display

### **Software**
*   **Dashboard:** Web-based interface (HTML/CSS/JavaScript)
*   **Mapping API:** OpenStreetMap

---
<p align="center">
  Made with ❤️, ☕, and minimal sleep by <b>Team Node</b> at Aavishkar 2026. <br>
</p>
