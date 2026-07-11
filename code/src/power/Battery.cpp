#include "Battery.h"
#include <Wire.h>
#include <math.h>
#include "../config.h"

bool Battery::begin() {
    // STEMMA QT connector power (and its I2C pull-ups) is gated by this pin
    // on the Feather ESP32-S3 — without it the gauge is unreachable.
    pinMode(I2C_POWER, OUTPUT);
    digitalWrite(I2C_POWER, HIGH);
    delay(10); // let the gauge power up before probing it

    Wire.begin(I2C_SDA, I2C_SCL);
    m_available = m_gauge.begin(&Wire);
    if (!m_available) {
        Serial.println("Battery: MAX17048 not found on I2C bus");
        return false;
    }
    Serial.printf("Battery: MAX17048 found, chip ID 0x%02X\n", m_gauge.getChipID());

    // Right after power-up the gauge's first ADC conversion isn't ready yet,
    // so VCELL briefly reads as a clean, ACK'd — but meaningless — 0.000V.
    // quickStart() forces an immediate recompute instead of waiting for the
    // normal conversion cycle; the settle delay gives that time to land
    // before update() takes its first (trusted) reading.
    m_gauge.quickStart();
    delay(250);
    update(true);
    return true;
}

void Battery::update(bool force) {
    if (!m_available) return;

    unsigned long now = millis();
    if (!force && m_last_update_ms != 0 && (now - m_last_update_ms) < UPDATE_INTERVAL_MS) return;
    m_last_update_ms = now;

    // A failed/garbage I2C read comes back as NaN, not some clamped-looking
    // number — checking for it explicitly (as Adafruit's own example does)
    // matters because NaN silently survives a naive `< 0` / `> 100` clamp
    // (neither comparison is ever true for NaN) and rounds down to 0, which
    // looks identical to "battery is empty" instead of "read failed". A
    // ~0V reading is the other face of the same problem — a connected LiPo
    // never actually reads that low — so it gets the same treatment: keep
    // the last known good value rather than trust an implausible one.
    float voltage = m_gauge.cellVoltage();
    float pct     = m_gauge.cellPercent();
    if (isnan(voltage) || isnan(pct) || voltage < 0.5f) {
        Serial.printf("Battery: bad reading from MAX17048 (%.3f V) — keeping last known value\n", voltage);
        return;
    }

    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    m_percent = (int)(pct + 0.5f);

    // Charging, inferred from whether voltage is actually rising rather
    // than the gauge's own charge-rate register (see Battery.h — that
    // turned out to sit at 0 in practice even while genuinely charging).
    if (m_trend_ref_ms == 0) {
        m_trend_ref_v  = voltage;
        m_trend_ref_ms = now;
    } else if (now - m_trend_ref_ms >= TREND_INTERVAL_MS) {
        m_charging     = (voltage - m_trend_ref_v) > TREND_RISE_THRESHOLD_V;
        m_trend_ref_v  = voltage;
        m_trend_ref_ms = now;
    }

    float rate = m_gauge.chargeRate();
    Serial.printf("Battery: %.3f V, %d%%, gauge_rate=%.2f%%/hr, charging=%d\n",
                  voltage, m_percent, rate, m_charging);
}
