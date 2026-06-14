/**
 * Main Application JavaScript
 *
 * This file contains the readable, maintainable JavaScript for the web interface.
 * It is automatically minified during build and embedded into the firmware.
 *
 * DO NOT EDIT web_interface.h directly - edit this file instead!
 *
 * To rebuild web_interface.h after making changes:
 *   python tools/minify_web.py
 */

function getCurrentTranslations() {
    return translationsCache || DEFAULT_TRANSLATIONS;
}

function setTranslationsCache(t) {
    if (t && typeof t === 'object') {
        translationsCache = Object.assign({}, DEFAULT_TRANSLATIONS, t);
    } else {
        translationsCache = DEFAULT_TRANSLATIONS;
    }
}

function fetchTranslations(lang) {
    const params = ['ts=' + Date.now()];
    if (lang) {
        params.push('lang=' + encodeURIComponent(lang));
    }
    const endpoint = '/api/get-translations?' + params.join('&');
    return fetch(endpoint, {
        cache: 'no-store'
    }).then(r => {
        if (!r.ok) throw new Error('translation fetch failed');
        return r.json();
    });
}

function refetchTranslations() {
    return fetchTranslations(currentLang).then(t => {
        setTranslationsCache(t);
        updateInterfaceTexts();
        return t;
    });
}

function tr(key) {
    const translations = getCurrentTranslations();
    const source = translations && translations[key];
    const fallback = DEFAULT_TRANSLATIONS[key];
    return typeof source === 'string' ? source : (typeof fallback === 'string' ? fallback : key);
}

function clearTranslationAttributes(el) {
    if (!el) return;
    el.removeAttribute('data-i18n');
    el.removeAttribute('data-i18n-prefix');
    el.removeAttribute('data-i18n-suffix');
    if (el.attributes) {
        const toRemove = [];
        for (let i = 0; i < el.attributes.length; i++) {
            const name = el.attributes[i].name;
            if (name && name.indexOf('data-i18n-replace-') === 0) {
                toRemove.push(name);
            }
        }
        toRemove.forEach(attr => el.removeAttribute(attr));
    }
}

function collectReplacementAttributes(el) {
    const replacements = {};
    if (!el || !el.attributes) {
        return replacements;
    }
    for (let i = 0; i < el.attributes.length; i++) {
        const attr = el.attributes[i];
        if (!attr || !attr.name || attr.name.indexOf('data-i18n-replace-') !== 0) {
            continue;
        }
        const key = attr.name.substring(18);
        if (key) {
            replacements[key] = attr.value;
        }
    }
    return replacements;
}

function resolveTranslationValue(key, el, translations) {
    if (!key) {
        return undefined;
    }
    const source = translations && translations[key];
    const fallback = DEFAULT_TRANSLATIONS[key];
    let value = typeof source === 'string' ? source : (typeof fallback === 'string' ? fallback : undefined);
    if (typeof value !== 'string') {
        return undefined;
    }
    const replacements = collectReplacementAttributes(el);
    Object.keys(replacements).forEach(name => {
        const raw = replacements[name];
        if (typeof raw === 'undefined') {
            return;
        }
        const text = String(raw);
        const lower = '{' + name + '}';
        const upper = '{' + name.toUpperCase() + '}';
        const percent = '%' + name.toUpperCase() + '%';
        value = value.split(lower).join(text);
        value = value.split(upper).join(text);
        value = value.split(percent).join(text);
    });
    return value;
}

function translateElement(el, translations) {
    if (!el) {
        return;
    }
    const key = el.getAttribute('data-i18n');
    if (!key) {
        return;
    }
    const value = resolveTranslationValue(key, el, translations);
    if (typeof value !== 'string') {
        return;
    }
    const prefix = el.getAttribute('data-i18n-prefix') || '';
    const suffix = el.getAttribute('data-i18n-suffix') || '';
    el.textContent = prefix + value + suffix;
}

function applyPlaceholderTranslation(el, translations) {
    if (!el) {
        return;
    }
    const key = el.getAttribute('data-i18n-placeholder');
    if (!key) {
        return;
    }
    const value = resolveTranslationValue(key, el, translations);
    if (typeof value !== 'string') {
        return;
    }
    const prefix = el.getAttribute('data-i18n-prefix') || '';
    const suffix = el.getAttribute('data-i18n-suffix') || '';
    el.setAttribute('placeholder', prefix + value + suffix);
}

function setElementTranslation(el, config) {
    if (!el) {
        return;
    }
    if (!config || typeof config.key !== 'string') {
        clearTranslationAttributes(el);
        if (config && typeof config.text === 'string') {
            el.textContent = config.text;
        }
        return;
    }
    clearTranslationAttributes(el);
    el.setAttribute('data-i18n', config.key);
    if (Object.prototype.hasOwnProperty.call(config, 'prefix')) {
        if (config.prefix) {
            el.setAttribute('data-i18n-prefix', config.prefix);
        } else {
            el.removeAttribute('data-i18n-prefix');
        }
    }
    if (Object.prototype.hasOwnProperty.call(config, 'suffix')) {
        if (config.suffix) {
            el.setAttribute('data-i18n-suffix', config.suffix);
        } else {
            el.removeAttribute('data-i18n-suffix');
        }
    }
    if (el.attributes) {
        const toRemove = [];
        for (let i = 0; i < el.attributes.length; i++) {
            const name = el.attributes[i].name;
            if (name && name.indexOf('data-i18n-replace-') === 0) {
                toRemove.push(name);
            }
        }
        toRemove.forEach(attr => el.removeAttribute(attr));
    }
    if (config.replacements && typeof config.replacements === 'object') {
        Object.keys(config.replacements).forEach(name => {
            if (typeof name !== 'string') {
                return;
            }
            el.setAttribute('data-i18n-replace-' + name, String(config.replacements[name]));
        });
    }
    translateElement(el, getCurrentTranslations());
}

function updatePlaceholderAttributes(translations) {
    document.querySelectorAll('[data-placeholder-key]').forEach(el => {
        const key = el.getAttribute('data-placeholder-key');
        if (!key) {
            return;
        }
        const value = resolveTranslationValue(key, el, translations);
        if (typeof value === 'string') {
            el.setAttribute('data-placeholder', value);
        }
    });
}

function updateInterfaceTexts(t) {
    if (t) {
        setTranslationsCache(t);
    }
    const translations = getCurrentTranslations();
    document.querySelectorAll('[data-i18n]').forEach(el => translateElement(el, translations));
    document.querySelectorAll('[data-i18n-placeholder]').forEach(el => applyPlaceholderTranslation(el, translations));
    updatePlaceholderAttributes(translations);
}
const SECURE_PROBE_TIMEOUT = 1500;
const securePreferenceCache = typeof Map === 'function' ? new Map() : null;
const securePreferenceStore = {};

function getSecurePreference(key) {
    if (securePreferenceCache) {
        return securePreferenceCache.has(key) ? securePreferenceCache.get(key) : undefined;
    }
    return Object.prototype.hasOwnProperty.call(securePreferenceStore, key) ? securePreferenceStore[key] : undefined;
}

function setSecurePreference(key, value) {
    if (securePreferenceCache) {
        securePreferenceCache.set(key, value);
        return;
    }
    securePreferenceStore[key] = value;
}

function probeSecureEndpoint(host, secure, cb) {
    if (!host || typeof cb !== 'function') {
        return;
    }
    const base = secure + host;
    let settled = false;
    const finalize = result => {
        if (settled) {
            return;
        }
        settled = true;
        clearTimeout(timer);
        cb(result);
    };
    const timer = setTimeout(() => finalize(false), SECURE_PROBE_TIMEOUT);
    if (typeof fetch === 'function') {
        fetch(base + '/', {
            mode: 'no-cors'
        }).then(() => finalize(true)).catch(() => finalize(false));
    } else {
        try {
            const tester = new Image();
            tester.onload = () => finalize(true);
            tester.onerror = () => finalize(false);
            tester.src = base + '/favicon.ico?probe=' + Date.now();
        } catch (err) {
            finalize(false);
        }
    }
}

function shouldPreferSecureAccess(host, secure, legacy) {
    if (!host || !secure || secure === legacy) {
        return false;
    }
    const key = secure + host;
    const cached = getSecurePreference(key);
    if (typeof cached === 'boolean') {
        return cached;
    }
    setSecurePreference(key, false);
    probeSecureEndpoint(host, secure, (result) => {
        if (result) {
            setSecurePreference(key, true);
        }
    });
    return false;
}

function applyAccessLinkScheme() {
    var links = document.querySelectorAll('[data-access-host]');
    for (var i = 0; i < links.length; i++) {
        var link = links[i];
        if (!link) {
            continue;
        }
        var host = link.getAttribute('data-access-host');
        var secure = link.getAttribute('data-secure') || 'https://';
        var legacy = link.getAttribute('data-legacy') || 'http://';
        var labelId = link.getAttribute('data-label-id');
        var labelNode = labelId ? document.getElementById(labelId) : null;
        var labelHost = link.getAttribute('data-access-label') || host || '';
        var legacyLabel = link.getAttribute('data-legacy-label') || '';
        if (!host) {
            link.href = '#';
            link.classList.add('disabled');
            link.setAttribute('aria-disabled', 'true');
            if (labelNode && labelNode.getAttribute('data-placeholder')) {
                labelNode.textContent = labelNode.getAttribute('data-placeholder');
            }
            continue;
        }
        var useSecure = shouldPreferSecureAccess(host, secure, legacy);
        var scheme = useSecure ? secure : legacy;
        link.href = scheme + host;
        link.classList.remove('disabled');
        link.setAttribute('aria-disabled', 'false');
        if (labelNode) {
            if (!useSecure && legacyLabel) {
                labelNode.textContent = legacyLabel;
            } else {
                labelNode.textContent = scheme + labelHost;
            }
        }
    }
}
document.addEventListener('DOMContentLoaded', () => {
    fetchTranslations(currentLang).then(t => {
        setTranslationsCache(t);
        updateInterfaceTexts();
    }).catch(err => {
        console.warn('Translations unavailable', err);
        setTimeout(() => refetchTranslations().catch(retryErr => console.error('Translations retry failed', retryErr)), 1000);
    });
    initNavigation();
    applyAccessLinkScheme();
    loadAllData();
    startAutoUpdate();
});

function startAutoUpdate() {
    if (updateTimer) clearInterval(updateTimer);
    updateTimer = setInterval(() => {
        if (isConnected) updateLiveData();
    }, UPDATE_INTERVAL);
}
async function loadAllData() {
    showUpdateIndicator();
    try {
        await Promise.all([updateSystemInfo(), updateMemoryInfo(), updateWiFiInfo(), updatePeripheralsInfo()]);
        isConnected = true;
        updateStatusIndicator(true);
    } catch (error) {
        console.error('Erreur:', error);
        isConnected = false;
        updateStatusIndicator(false);
    }
    hideUpdateIndicator();
}
async function updateLiveData() {
    try {
        const response = await fetch('/api/status');
        const data = await response.json();
        updateRealtimeValues(data);
        isConnected = true;
        updateStatusIndicator(true);
    } catch (error) {
        console.error('Erreur:', error);
        isConnected = false;
        updateStatusIndicator(false);
    }
}
async function updateSystemInfo() {
    const r = await fetch('/api/system-info');
    const d = await r.json();
    const chipModelEl = document.getElementById('chipModel');
    if (chipModelEl) {
        chipModelEl.textContent = d.chipModel || '';
    }
    const ipLabel = document.getElementById('ipAddressText');
    const ipLink = document.getElementById('ipAddressLink');
    const hasIp = d.ipAddress && d.ipAddress.length;
    const secureScheme = (ipLink && ipLink.getAttribute('data-secure')) || 'https://';
    const legacyScheme = (ipLink && ipLink.getAttribute('data-legacy')) || 'http://';
    if (ipLabel) {
        if (hasIp) {
            clearTranslationAttributes(ipLabel);
            ipLabel.textContent = legacyScheme + d.ipAddress;
        } else {
            ipLabel.setAttribute('data-i18n', 'ip_unavailable');
            translateElement(ipLabel, getCurrentTranslations());
        }
    }
    if (ipLink) {
        if (hasIp) {
            ipLink.href = legacyScheme + d.ipAddress;
            ipLink.setAttribute('data-access-host', d.ipAddress);
            ipLink.setAttribute('data-access-label', d.ipAddress);
            ipLink.setAttribute('data-legacy-label', legacyScheme + d.ipAddress);
            ipLink.setAttribute('aria-disabled', 'false');
            ipLink.classList.remove('disabled');
        } else {
            ipLink.href = '#';
            ipLink.setAttribute('data-access-host', '');
            ipLink.setAttribute('data-access-label', '');
            ipLink.setAttribute('data-legacy-label', '');
            ipLink.setAttribute('aria-disabled', 'true');
            ipLink.classList.add('disabled');
        }
    }
    applyAccessLinkScheme();
}
async function updateMemoryInfo() {
    await fetch('/api/memory');
}
async function updateWiFiInfo() {
    await fetch('/api/wifi-info');
}
async function updatePeripheralsInfo() {
    await fetch('/api/peripherals');
}

function showTab(tabName, btn) {
    var contents = document.querySelectorAll('.tab-content');
    for (var i = 0; i < contents.length; i++) {
        contents[i].classList.remove('active');
    }
    var tab = document.getElementById(tabName);
    if (tab) {
        tab.classList.add('active');
    } else {
        loadTab(tabName);
    }
    setActiveTabButton(tabName, btn);
}

function setActiveTabButton(tabName, btn) {
    var buttons = document.querySelectorAll('.nav-btn');
    for (var i = 0; i < buttons.length; i++) {
        buttons[i].classList.remove('active');
    }
    if (btn && btn.classList) {
        btn.classList.add('active');
        return;
    }
    var selector = '.nav-btn[data-tab="' + tabName + '"]';
    var fallback = document.querySelector(selector);
    if (fallback) {
        fallback.classList.add('active');
    }
}

function findNavButton(el) {
    while (el && el.classList && !el.classList.contains('nav-btn')) {
        el = el.parentElement;
    }
    if (el && el.classList && el.classList.contains('nav-btn')) {
        return el;
    }
    return null;
}

function initNavigation() {
    var navs = document.querySelectorAll('.nav');
    if (!navs || !navs.length) {
        showTab('overview');
        return;
    }
    for (var n = 0; n < navs.length; n++) {
        (function(nav) {
            nav.addEventListener('click', function(e) {
                e.preventDefault();
                var btn = findNavButton(e.target);
                if (!btn) return;
                var target = btn.getAttribute('data-tab');
                if (target) {
                    showTab(target, btn);
                }
            });;
        })(navs[n]);
    }
    var active = document.querySelector('.nav-btn.active');
    if (!active) {
        var list = document.querySelectorAll('.nav-btn');
        if (list.length > 0) {
            active = list[0];
        }
    }
    if (active) {
        showTab(active.getAttribute('data-tab'), active);
    } else {
        showTab('overview');
    }
}
async function loadTab(tabName) {
    const c = document.getElementById('tabContainer');
    let tab = document.getElementById(tabName);
    if (!tab) {
        tab = document.createElement('div');
        tab.id = tabName;
        tab.className = 'tab-content';
        c.appendChild(tab);
    }
    tab.innerHTML = '<div class="section"><div class="loading"></div><p style="text-align:center" data-i18n="loading">' + tr('loading') + '</p></div>';
    tab.classList.add('active');
    try {
        if (tabName === 'overview') {
            const r = await fetch('/api/overview');
            const d = await r.json();
            tab.innerHTML = buildOverview(d);
        } else if (tabName === 'display-signal') {
            const leds = await fetch('/api/leds-info');
            const screens = await fetch('/api/screens-info');
            const ld = await leds.json();
            const sd = await screens.json();
            tab.innerHTML = buildDisplaySignal(ld, sd);
        } else if (tabName === 'sensors') {
            tab.innerHTML = buildSensors();
            loadEnvironmentalData();
        } else if (tabName === 'input-devices') {
            tab.innerHTML = buildInputDevices();
        } else if (tabName === 'memory') {
            tab.innerHTML = buildMemory();
        } else if (tabName === 'hardware-tests') {
            tab.innerHTML = buildHardwareTests();
        } else if (tabName === 'wireless') {
            tab.innerHTML = buildWireless();
            loadWirelessInfo();
        } else if (tabName === 'benchmark') {
            tab.innerHTML = buildBenchmark();
        } else if (tabName === 'export') {
            tab.innerHTML = buildExport();
        }
        updateInterfaceTexts();
    } catch (e) {
        tab.innerHTML = '<div class="section"><h2 data-i18n="error_label" data-i18n-prefix="❌">' + tr('error_label') + '</h2><p>' + String(e) + '</p></div>';
        updateInterfaceTexts();
    }
}

function buildOverview(d) {
    let h = '<div class="section"><h2 data-i18n="chip_info" data-i18n-prefix="🔧">' + tr('chip_info') + '</h2><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="full_model">' + tr('full_model') + '</div><div class="info-value">' + d.chip.model + ' <span data-i18n="revision">' + tr('revision') + '</span> ' + d.chip.revision + '</div></div>';
    const cpuSummary = d.chip.cores + ' <span data-i18n="cores">' + tr('cores') + '</span> @ ' + d.chip.freq + ' MHz';
    h += '<div class="info-item"><div class="info-label" data-i18n="cpu_cores">' + tr('cpu_cores') + '</div><div class="info-value">' + cpuSummary + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="mac_wifi">' + tr('mac_wifi') + '</div><div class="info-value">' + d.chip.mac + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="uptime">' + tr('uptime') + '</div><div class="info-value" id="uptime">' + formatUptime(d.chip.uptime) + '</div></div>';
    if (d.chip.temperature !== -999) {
        h += '<div class="info-item"><div class="info-label" data-i18n="cpu_temp">' + tr('cpu_temp') + '</div><div class="info-value" id="temperature">' + d.chip.temperature.toFixed(1) + ' °C</div></div>';
    }
    h += '</div></div>';
    h += '<div class="section"><h2 data-i18n="memory_details" data-i18n-prefix="💾">' + tr('memory_details') + '</h2>';
    h += '<h3 data-i18n="flash_memory" data-i18n-prefix="📦">' + tr('flash_memory') + '</h3><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="real_size">' + tr('real_size') + '</div><div class="info-value">' + (d.memory.flash.real / 1048576).toFixed(2) + ' MB</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="flash_type">' + tr('flash_type') + '</div><div class="info-value">' + d.memory.flash.type + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="flash_speed">' + tr('flash_speed') + '</div><div class="info-value">' + d.memory.flash.speed + ' MHz</div></div>';
    h += '</div>';
    h += '<h3 data-i18n="internal_sram" data-i18n-prefix="🧠">' + tr('internal_sram') + '</h3><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="total_size">' + tr('total_size') + '</div><div class="info-value" id="sram-total">' + (d.memory.sram.total / 1024).toFixed(2) + ' KB</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="free">' + tr('free') + '</div><div class="info-value" id="sram-free">' + (d.memory.sram.free / 1024).toFixed(2) + ' KB</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="used">' + tr('used') + '</div><div class="info-value" id="sram-used">' + (d.memory.sram.used / 1024).toFixed(2) + ' KB</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="memory_fragmentation">' + tr('memory_fragmentation') + '</div><div class="info-value" id="fragmentation">' + d.memory.fragmentation.toFixed(1) + '%</div></div>';
    h += '</div>';
    const sramPct = ((d.memory.sram.used / d.memory.sram.total) * 100).toFixed(1);
    h += '<div class="progress-bar"><div class="progress-fill" id="sram-progress" style="width:' + sramPct + '%">' + sramPct + '%</div></div>';
    if (d.memory.psram.total > 0) {
        h += '<h3 data-i18n="psram_external" data-i18n-prefix="📦">' + tr('psram_external') + '</h3><div class="info-grid">';
        h += '<div class="info-item"><div class="info-label" data-i18n="total_size">' + tr('total_size') + '</div><div class="info-value" id="psram-total">' + (d.memory.psram.total / 1048576).toFixed(2) + ' MB</div></div>';
        h += '<div class="info-item"><div class="info-label" data-i18n="free">' + tr('free') + '</div><div class="info-value" id="psram-free">' + (d.memory.psram.free / 1048576).toFixed(2) + ' MB</div></div>';
        h += '<div class="info-item"><div class="info-label" data-i18n="used">' + tr('used') + '</div><div class="info-value" id="psram-used">' + (d.memory.psram.used / 1048576).toFixed(2) + ' MB</div></div>';
        h += '</div>';
        const psramPct = ((d.memory.psram.used / d.memory.psram.total) * 100).toFixed(1);
        h += '<div class="progress-bar"><div class="progress-fill" id="psram-progress" style="width:' + psramPct + '%">' + psramPct + '%</div></div>';
    }
    h += '</div>';
    h += '<div class="section"><h2 data-i18n="wifi_connection" data-i18n-prefix="📡">' + tr('wifi_connection') + '</h2><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="connected_ssid">' + tr('connected_ssid') + '</div><div class="info-value">' + (d.wifi.ssid || '') + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="signal_power">' + tr('signal_power') + '</div><div class="info-value">' + d.wifi.rssi + ' dBm</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="signal_quality">' + tr('signal_quality') + '</div><div class="info-value">' + (d.wifi.quality_key ? tr(d.wifi.quality_key) : d.wifi.quality) + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="ip_address">' + tr('ip_address') + '</div><div class="info-value">' + (d.wifi.ip || '') + '</div></div>';
    h += '</div></div>';
    h += '<div class="section"><h2 data-i18n="gpio_interfaces" data-i18n-prefix="🔌">' + tr('gpio_interfaces') + '</h2><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="total_gpio">' + tr('total_gpio') + '</div><div class="info-value">' + d.gpio.total + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="i2c_peripherals">' + tr('i2c_peripherals') + '</div><div class="info-value">' + d.gpio.i2c_count + '</div></div>';
    h += '<div class="info-item" style="grid-column:1/-1"><div class="info-label" data-i18n="detected_addresses">' + tr('detected_addresses') + '</div><div class="info-value">' + (d.gpio.i2c_devices || '') + '</div></div>';
    h += '</div></div>';
    return h;
}

function buildLeds(d) {
    let h = '<div class="section"><h2 data-i18n="builtin_led" data-i18n-prefix="💡">' + tr('builtin_led') + '</h2><p data-i18n="builtin_led_desc">' + tr('builtin_led_desc') + '</p><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="gpio">' + tr('gpio') + '</div><div class="info-value"><span data-i18n="gpio">' + tr('gpio') + '</span> ' + d.builtin.pin + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="status">' + tr('status') + '</div><div class="info-value" id="builtin-led-status">' + (d.builtin.status || '') + '</div></div>';
    h += '<div class="info-item" style="grid-column:1/-1;text-align:center">';
    h += '<strong data-i18n="configure_led_pin">' + tr('configure_led_pin') + '</strong><br>';
    h += '<span data-i18n="gpio">' + tr('gpio') + '</span>: <input type="number" id="builtin-led-gpio" value="' + d.builtin.pin + '" min="0" max="48" style="width:80px;padding:5px;margin:5px;border:1px solid #ccc;border-radius:5px"> ';
    h += '<button class="btn btn-primary" data-i18n="apply_config" data-i18n-prefix="⚙️" onclick="configBuiltinLED()">' + tr('apply_config') + '</button><br><br>';
    h += '<button class="btn btn-primary" data-i18n="full_test" data-i18n-prefix="🧪" onclick="testBuiltinLED()">' + tr('full_test') + '</button> ';
    h += '<button class="btn btn-success" data-i18n="blink" data-i18n-prefix="⚡" onclick="ledBlink()">' + tr('blink') + '</button> ';
    h += '<button class="btn btn-info" data-i18n="fade" data-i18n-prefix="🌊" onclick="ledFade()">' + tr('fade') + '</button> ';
    h += '<button class="btn btn-warning" data-i18n="turn_on" data-i18n-prefix="💡" onclick="ledOn()">' + tr('turn_on') + '</button> ';
    h += '<button class="btn btn-danger" data-i18n="turn_off" data-i18n-prefix="⭕" onclick="ledOff()">' + tr('turn_off') + '</button>';
    h += '</div></div></div>';
    h += '<div class="section"><h2 data-i18n="neopixel" data-i18n-prefix="🌈">' + tr('neopixel') + '</h2><p data-i18n="neopixel_desc">' + tr('neopixel_desc') + '</p><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="gpio">' + tr('gpio') + '</div><div class="info-value"><span data-i18n="gpio">' + tr('gpio') + '</span> ' + d.neopixel.pin + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="led_count">' + tr('led_count') + '</div><div class="info-value">' + d.neopixel.count + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="status">' + tr('status') + '</div><div class="info-value" id="neopixel-status">' + (d.neopixel.status || '') + '</div></div>';
    h += '<div class="info-item" style="grid-column:1/-1;text-align:center">';
    h += '<strong data-i18n="configure_neopixel">' + tr('configure_neopixel') + '</strong><br>';
    h += '<span data-i18n="gpio">' + tr('gpio') + '</span>: <input type="number" id="neopixel-gpio" value="' + d.neopixel.pin + '" min="0" max="48" style="width:80px;padding:5px;margin:5px;border:1px solid #ccc;border-radius:5px"> ';
    h += '<span data-i18n="led_count">' + tr('led_count') + '</span>: <input type="number" id="neopixel-count" value="' + d.neopixel.count + '" min="1" max="100" style="width:80px;padding:5px;margin:5px;border:1px solid #ccc;border-radius:5px"> ';
    h += '<button class="btn btn-primary" data-i18n="apply_config" data-i18n-prefix="⚙️" onclick="configNeoPixel()">' + tr('apply_config') + '</button><br><br>';
    h += '<button class="btn btn-primary" data-i18n="full_test" data-i18n-prefix="🧪" onclick="testNeoPixel()">' + tr('full_test') + '</button><br><br>';
    h += '<strong data-i18n="animations" data-i18n-suffix=" :">' + tr('animations') + '</strong><br>';
    h += '<button class="btn btn-primary" data-i18n="rainbow" data-i18n-prefix="🌈" onclick="neoPattern(\'rainbow\')">' + tr('rainbow') + '</button> ';
    h += '<button class="btn btn-success" data-i18n="blink" data-i18n-prefix="⚡" onclick="neoPattern(\'blink\')">' + tr('blink') + '</button> ';
    h += '<button class="btn btn-info" data-i18n="fade" data-i18n-prefix="🌊" onclick="neoPattern(\'fade\')">' + tr('fade') + '</button> ';
    h += '<button class="btn btn-warning" data-i18n="chase" data-i18n-prefix="🏃" onclick="neoPattern(\'chase\')">' + tr('chase') + '</button><br><br>';
    h += '<strong data-i18n="custom_color" data-i18n-suffix=" :">' + tr('custom_color') + '</strong><br>';
    h += '<input type="color" id="neoColor" value="#ff0000" style="height:50px;width:120px;border:none;border-radius:5px;cursor:pointer"> ';
    h += '<button class="btn btn-primary" data-i18n="apply_color" data-i18n-prefix="🎨" onclick="neoCustomColor()">' + tr('apply_color') + '</button><br><br>';
    h += '<button class="btn btn-danger" data-i18n="turn_off_all" data-i18n-prefix="⭕" onclick="neoPattern(\'off\')">' + tr('turn_off_all') + '</button>';
    h += '</div></div></div>';
    return h;
}

function buildScreens(d) {
    const rotation = (typeof d.oled.rotation !== 'undefined') ? d.oled.rotation : 0;
    const hasOled = d && d.oled && ((typeof d.oled.available === 'undefined') ? true : !!d.oled.available);
    const hasTft = d && d.tft && ((typeof d.tft.available === 'undefined') ? true : !!d.tft.available);
    let h = '<div class="section"><h2 data-i18n="oled_screen" data-i18n-prefix="🖥️">' + tr('oled_screen') + '</h2><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="status">' + tr('status') + '</div><div class="info-value" id="oled-status">' + (d.oled.status || '') + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="i2c_pins">' + tr('i2c_pins') + '</div><div class="info-value" id="oled-pins"><span data-i18n="label_sda" data-i18n-suffix=" :">' + tr('label_sda') + '</span>' + d.oled.pins.sda + ' <span data-i18n="label_scl" data-i18n-suffix=" :">' + tr('label_scl') + '</span>' + d.oled.pins.scl + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="rotation">' + tr('rotation') + '</div><div class="info-value" id="oled-rotation-display">' + rotation + '</div></div>';
    h += '</div>';
    h += '<div class="info-item" style="grid-column:1/-1;text-align:center">';
    h += '<span data-i18n="label_sda" data-i18n-suffix=" :">' + tr('label_sda') + '</span><input type="number" id="oledSDA" value="' + d.oled.pins.sda + '" min="0" max="48" style="width:70px"> ';
    h += '<span data-i18n="label_scl" data-i18n-suffix=" :">' + tr('label_scl') + '</span><input type="number" id="oledSCL" value="' + d.oled.pins.scl + '" min="0" max="48" style="width:70px"> ';
    h += '<br><span data-i18n="rotation" data-i18n-suffix=" :">' + tr('rotation') + '</span> <select id="oledRotation" style="width:90px;padding:10px;border:2px solid #ddd;border-radius:5px">';
    for (let i = 0; i < 4; i++) {
        h += '<option value=\'' + i + '\'' + (i === rotation ? ' selected' : '') + '>' + i + '</option>';
    }
    h += '</select> ';
    h += 'Width: <input type="number" id="oledWidth" value="' + (d.oled.width || 128) + '" min="32" max="256" style="width:70px"> ';
    h += 'Height: <input type="number" id="oledHeight" value="' + (d.oled.height || 64) + '" min="32" max="128" style="width:70px"><br>';
    h += '<button class="btn btn-info" data-i18n="apply_redetect" data-i18n-prefix="🔄" onclick="configOLED()">' + tr('apply_redetect') + '</button>';
    h += '</div>';
    if (hasOled) {
        h += '<div style="margin-top:15px"><button class="btn btn-primary" data-i18n="full_test" data-i18n-prefix="🧪" data-i18n-suffix=" (25s)" onclick="testOLED()">' + tr('full_test') + '</button> <button class="btn btn-success" data-i18n="boot_screen" data-i18n-prefix="🏠" onclick="oledBoot()">' + tr('boot_screen') + '</button></div>';
        h += '<div class="oled-step-grid" style="margin-top:15px;display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px">';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_welcome" data-i18n-prefix="🏁" onclick="oledStep(\'welcome\')">' + tr('oled_step_welcome') + '</button>';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_big_text" data-i18n-prefix="🔠" onclick="oledStep(\'big_text\')">' + tr('oled_step_big_text') + '</button>';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_text_sizes" data-i18n-prefix="🔤" onclick="oledStep(\'text_sizes\')">' + tr('oled_step_text_sizes') + '</button>';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_shapes" data-i18n-prefix="🟦" onclick="oledStep(\'shapes\')">' + tr('oled_step_shapes') + '</button>';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_horizontal_lines" data-i18n-prefix="📏" onclick="oledStep(\'horizontal_lines\')">' + tr('oled_step_horizontal_lines') + '</button>';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_diagonals" data-i18n-prefix="📐" onclick="oledStep(\'diagonals\')">' + tr('oled_step_diagonals') + '</button>';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_moving_square" data-i18n-prefix="[SQ]" onclick="oledStep(\'moving_square\')">' + tr('oled_step_moving_square') + '</button>';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_progress_bar" data-i18n-prefix="📊" onclick="oledStep(\'progress_bar\')">' + tr('oled_step_progress_bar') + '</button>';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_scroll_text" data-i18n-prefix="📜" onclick="oledStep(\'scroll_text\')">' + tr('oled_step_scroll_text') + '</button>';
        h += '<button class="btn btn-secondary" data-i18n="oled_step_final_message" data-i18n-prefix="[OK]" onclick="oledStep(\'final_message\')">' + tr('oled_step_final_message') + '</button>';
        h += '</div>';
        h += '<div style="margin-top:15px">';
        h += '<label for="oledText" style="display:block;margin-bottom:8px;font-weight:bold;color:#667eea" data-i18n="custom_message">' + tr('custom_message') + '</label>';
        h += '<textarea id="oledText" rows="3" style="width:100%;padding:10px;border:2px solid #ddd;border-radius:8px" data-i18n-placeholder="custom_message" placeholder="' + tr('custom_message') + '"></textarea>';
        h += '<div style="margin-top:10px"><button class="btn btn-success" data-i18n="show_message" data-i18n-prefix="📤" onclick="oledDisplayText()">' + tr('show_message') + '</button></div>';
        h += '<p style="margin-top:12px;color:#555" data-i18n="changes_pins">' + tr('changes_pins') + '</p>';
    } else {
        h += '<p class="status-live error" data-i18n="no_detected">' + tr('no_detected') + '</p>';
        h += '<p style="margin-top:10px;color:#555" data-i18n="check_wiring">' + tr('check_wiring') + '</p>';
    }
    h += '</div></div>';
    if (d.tft) {
        h += '<div class="section"><h2 data-i18n="tft_screen" data-i18n-prefix="📱">' + tr('tft_screen') + '</h2><div class="info-grid">';
        h += '<div class="info-item"><div class="info-label" data-i18n="status">' + tr('status') + '</div><div class="info-value" id="tft-status">' + (d.tft.status || '') + '</div></div>';
        h += '<div class="info-item"><div class="info-label" data-i18n="resolution">' + tr('resolution') + '</div><div class="info-value">' + d.tft.width + ' x ' + d.tft.height + '</div></div>';
        h += '<div class="info-item"><div class="info-label" data-i18n="spi_pins">' + tr('spi_pins') + '</div><div class="info-value">MISO:' + d.tft.pins.miso + ' MOSI:' + d.tft.pins.mosi + ' SCLK:' + d.tft.pins.sclk + ' CS:' + d.tft.pins.cs + ' DC:' + d.tft.pins.dc + ' RST:' + d.tft.pins.rst + '</div></div>';
        h += '</div>';
        h += '<div class="info-item" style="grid-column:1/-1;text-align:center;margin-top:15px">';
        h += '<strong>Configuration TFT</strong><br>';
        h += 'MISO: <input type="number" id="tftMISO" value="' + d.tft.pins.miso + '" min="-1" max="48" style="width:60px"> ';
        h += 'MOSI: <input type="number" id="tftMOSI" value="' + d.tft.pins.mosi + '" min="0" max="48" style="width:60px"> ';
        h += 'SCLK: <input type="number" id="tftSCLK" value="' + d.tft.pins.sclk + '" min="0" max="48" style="width:60px"> ';
        h += 'CS: <input type="number" id="tftCS" value="' + d.tft.pins.cs + '" min="0" max="48" style="width:60px"> ';
        h += 'DC: <input type="number" id="tftDC" value="' + d.tft.pins.dc + '" min="0" max="48" style="width:60px"> ';
        h += 'RST: <input type="number" id="tftRST" value="' + d.tft.pins.rst + '" min="-1" max="48" style="width:60px"><br>';
        h += 'BL: <input type="number" id="tftBL" value="' + d.tft.pins.bl + '" min="-1" max="48" style="width:60px"> ';
        h += 'Width: <input type="number" id="tftWidth" value="' + d.tft.width + '" min="128" max="480" style="width:70px"> ';
        h += 'Height: <input type="number" id="tftHeight" value="' + d.tft.height + '" min="128" max="480" style="width:70px"> ';
        h += 'Rotation: <select id="tftRotation" style="width:70px;padding:5px">';
        for (let i = 0; i < 4; i++) {
            h += '<option value="' + i + '"' + (i === (d.tft.rotation || 0) ? ' selected' : '') + '>' + i + '</option>';
        }
        h += '</select><br>';
        h += ' <span style="margin-left:15px">Driver:</span> <select id="tftDriver" style="width:100px;padding:5px"><option value="ILI9341"' + (d.tft.driver === 'ILI9341' ? ' selected' : '') + '>ILI9341</option><option value="ST7789"' + (d.tft.driver === 'ST7789' ? ' selected' : '') + '>ST7789</option></select><br>';
        h += '<div style="margin-top:15px;padding:10px;background:#f0f8ff;border-radius:5px">';
        h += '<strong data-i18n="tft_brightness">' + tr('tft_brightness') + '</strong><br>';
        h += '<input type="range" id="tftBrightnessSlider" min="0" max="255" value="255" style="width:80%;margin:10px 0" oninput="updateBrightnessValue(this.value)" onchange="setTFTBrightnessLevel(this.value)">';
        h += '<span id="tftBrightnessValue" style="margin-left:10px;font-weight:bold">255</span> / 255<br>';
        h += '<button class="btn btn-sm" onclick="setTFTBrightnessLevel(0)" style="margin:2px">OFF</button> ';
        h += '<button class="btn btn-sm" onclick="setTFTBrightnessLevel(64)" style="margin:2px">25%</button> ';
        h += '<button class="btn btn-sm" onclick="setTFTBrightnessLevel(128)" style="margin:2px">50%</button> ';
        h += '<button class="btn btn-sm" onclick="setTFTBrightnessLevel(192)" style="margin:2px">75%</button> ';
        h += '<button class="btn btn-sm" onclick="setTFTBrightnessLevel(255)" style="margin:2px">100%</button>';
        h += '</div>';
        h += '<button class="btn btn-primary" data-i18n="apply_config" data-i18n-prefix="⚙️" onclick="configTFT()">' + tr('apply_config') + '</button>';
        h += '</div>';
        if (hasTft) {
            h += '<div style="margin-top:15px"><button class="btn btn-primary" data-i18n="full_test" data-i18n-prefix="🧪" onclick="testTFT()">' + tr('full_test') + '</button> <button class="btn btn-success" data-i18n="boot_screen" data-i18n-prefix="🏠" onclick="tftBoot()">' + tr('boot_screen') + '</button></div>';
            h += '<div class="tft-step-grid" style="margin-top:15px;display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px">';
            h += '<button class="btn btn-secondary" data-i18n="tft_step_boot" data-i18n-prefix="🏁" onclick="tftStep(\'boot\')">' + tr('tft_step_boot') + '</button>';
            h += '<button class="btn btn-secondary" data-i18n="tft_step_colors" data-i18n-prefix="🎨" onclick="tftStep(\'colors\')">' + tr('tft_step_colors') + '</button>';
            h += '<button class="btn btn-secondary" data-i18n="tft_step_shapes" data-i18n-prefix="🟦" onclick="tftStep(\'shapes\')">' + tr('tft_step_shapes') + '</button>';
            h += '<button class="btn btn-secondary" data-i18n="tft_step_text" data-i18n-prefix="🔤" onclick="tftStep(\'text\')">' + tr('tft_step_text') + '</button>';
            h += '<button class="btn btn-secondary" data-i18n="tft_step_lines" data-i18n-prefix="📏" onclick="tftStep(\'lines\')">' + tr('tft_step_lines') + '</button>';
            h += '<button class="btn btn-secondary" data-i18n="tft_step_animation" data-i18n-prefix="[SQ]" onclick="tftStep(\'animation\')">' + tr('tft_step_animation') + '</button>';
            h += '<button class="btn btn-secondary" data-i18n="tft_step_progress" data-i18n-prefix="📊" onclick="tftStep(\'progress\')">' + tr('tft_step_progress') + '</button>';
            h += '<button class="btn btn-secondary" data-i18n="tft_step_final" data-i18n-prefix="[OK]" onclick="tftStep(\'final\')">' + tr('tft_step_final') + '</button>';
            h += '</div>';
        } else {
            h += '<p class="status-live error" data-i18n="no_detected">' + tr('no_detected') + '</p>';
            h += '<p style="margin-top:10px;color:#555" data-i18n="check_wiring">' + tr('check_wiring') + '</p>';
        }
        h += '</div></div>';
    }
    return h;
}

function buildTests() {
    let h = '';
    h += '<div class="section"><h2 data-i18n="adc_test" data-i18n-prefix="📊">' + tr('adc_test') + '</h2>';
    h += '<p data-i18n="adc_desc">' + tr('adc_desc') + '</p>';
    h += '<div style="text-align:center;margin:20px 0"><button class="btn btn-primary" data-i18n="start_adc_test" data-i18n-prefix="▶️" onclick="testADC()">' + tr('start_adc_test') + '</button></div>';
    h += '<div id="adc-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div>';
    h += '<div id="adc-results" class="info-grid"></div></div>';
    h += '<div class="section"><h2 data-i18n="pwm_test" data-i18n-prefix="🎚️">' + tr('pwm_test') + '</h2>';
    h += '<p data-i18n="pwm_test_desc">' + tr('pwm_test_desc') + '</p>';
    h += '<div style="text-align:center;margin:20px 0"><button class="btn btn-primary" data-i18n="start_pwm_test" data-i18n-prefix="🎛️" onclick="runPWMTest()">' + tr('start_pwm_test') + '</button></div>';
    h += '<div id="pwm-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div></div>';
    h += '<div class="section"><h2 data-i18n="spi_scan" data-i18n-prefix="🧰">' + tr('spi_scan') + '</h2>';
    h += '<p data-i18n="spi_scan_desc">' + tr('spi_scan_desc') + '</p>';
    h += '<div style="text-align:center;margin:20px 0"><button class="btn btn-info" data-i18n="start_spi_scan" data-i18n-prefix="🔍" onclick="runSPIScan()">' + tr('start_spi_scan') + '</button></div>';
    h += '<div id="spi-status" class="status-live" data-i18n="click_to_scan">' + tr('click_to_scan') + '</div>';
    h += '<div id="spi-results" class="info-grid"></div></div>';
    h += '<div class="section"><h2 data-i18n="memory_stress" data-i18n-prefix="🔥">' + tr('memory_stress') + '</h2>';
    h += '<p data-i18n="stress_desc">' + tr('stress_desc') + '</p>';
    h += '<div style="text-align:center;margin:20px 0"><button class="btn btn-danger" data-i18n="start_stress" data-i18n-prefix="🚀" onclick="runStressTest()">' + tr('start_stress') + '</button></div>';
    h += '<p style="color:#dc3545;font-weight:bold;text-align:center" data-i18n="stress_warning" data-i18n-prefix="⚠️">' + tr('stress_warning') + '</p>';
    h += '<div id="stress-status" class="status-live" data-i18n="not_tested">' + tr('not_tested') + '</div>';
    h += '<div id="stress-results" class="info-grid"></div></div>';
    return h;
}

function buildGpio() {
    let h = '<div class="section"><h2 data-i18n="gpio_test" data-i18n-prefix="🔌">' + tr('gpio_test') + '</h2>';
    h += '<p data-i18n="gpio_desc">' + tr('gpio_desc') + '</p>';
    h += '<div style="text-align:center;margin:20px 0"><button class="btn btn-primary" data-i18n="test_all_gpio" data-i18n-prefix="🧪" onclick="testAllGPIO()">' + tr('test_all_gpio') + '</button></div>';
    h += '<div id="gpio-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div>';
    h += '<p style="margin-top:10px;color:#555" data-i18n="gpio_warning">' + tr('gpio_warning') + '</p>';
    h += '<div id="gpio-results" class="gpio-grid"></div></div>';
    return h;
}

function buildWireless() {
    let h = '<div class="section"><h2 data-i18n="wifi_scanner" data-i18n-prefix="📡">' + tr('wifi_scanner') + '</h2><p data-i18n="wireless_intro">' + tr('wireless_intro') + '</p>';
    h += '<div class="info-grid" id="current-wifi-info">';
    h += '<div class="info-item"><div class="info-label" data-i18n="wifi_status">' + tr('wifi_status') + '</div><div class="info-value" id="wifi-connected">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="wifi_ssid">' + tr('wifi_ssid') + '</div><div class="info-value" id="wifi-current-ssid">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="ip_address">' + tr('ip_address') + '</div><div class="info-value" id="wifi-ip">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="gateway">' + tr('gateway') + '</div><div class="info-value" id="wifi-gateway">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="dns_server">' + tr('dns_server') + '</div><div class="info-value" id="wifi-dns">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="wifi_rssi">' + tr('wifi_rssi') + '</div><div class="info-value" id="wifi-rssi">-</div></div>';
    h += '</div>';
    h += '<p data-i18n="wifi_desc">' + tr('wifi_desc') + '</p>';
    h += '<div style="text-align:center;margin:20px 0"><button class="btn btn-primary" data-i18n="scan_networks" data-i18n-prefix="🔍" onclick="scanWiFi()">' + tr('scan_networks') + '</button></div>';
    h += '<div id="wifi-status" class="status-live" data-i18n="click_to_scan">' + tr('click_to_scan') + '</div>';
    h += '<div id="wifi-results" class="wifi-list"></div></div>';
    h += '<div class="section"><h2 data-i18n="gps_module" data-i18n-prefix="🛰️">' + tr('gps_module') + '</h2><p data-i18n="gps_module_desc">' + tr('gps_module_desc') + '</p>';
    h += '<div class="card"><div class="info-grid" id="gps-info">';
    h += '<div class="info-item"><div class="info-label" data-i18n="gps_status">' + tr('gps_status') + '</div><div class="info-value" id="gps-status-value">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="gps_latitude">' + tr('gps_latitude') + '</div><div class="info-value" id="gps-latitude">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="gps_longitude">' + tr('gps_longitude') + '</div><div class="info-value" id="gps-longitude">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="gps_altitude">' + tr('gps_altitude') + '</div><div class="info-value" id="gps-altitude">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="gps_satellites">' + tr('gps_satellites') + '</div><div class="info-value" id="gps-satellites">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="gps_hdop">' + tr('gps_hdop') + '</div><div class="info-value" id="gps-hdop">-</div></div>';
    h += '</div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="loadGPSData()" data-i18n="refresh_gps" data-i18n-prefix="🔄">' + tr('refresh_gps') + '</button> ';
    h += '<button class="btn btn-info" onclick="testGPS()" data-i18n="test_gps" data-i18n-prefix="🧪">' + tr('test_gps') + '</button>';
    h += '</div><div id="gps-test-status" class="status-live"></div></div></div>';
    return h;
}

function buildBenchmark() {
    let h = '<div class="section"><h2 data-i18n="performance_bench" data-i18n-prefix="⚡">' + tr('performance_bench') + '</h2>';
    h += '<p data-i18n="benchmark_desc">' + tr('benchmark_desc') + '</p>';
    h += '<div style="text-align:center;margin:20px 0"><button class="btn btn-primary" data-i18n="run_benchmarks" data-i18n-prefix="🚀" onclick="runBenchmarks()">' + tr('run_benchmarks') + '</button></div>';
    h += '<div class="info-grid" id="benchmark-results">';
    h += '<div class="info-item"><div class="info-label" data-i18n="cpu_benchmark">' + tr('cpu_benchmark') + '</div><div class="info-value" id="cpu-bench" data-i18n="not_tested">' + tr('not_tested') + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="memory_benchmark">' + tr('memory_benchmark') + '</div><div class="info-value" id="mem-bench" data-i18n="not_tested">' + tr('not_tested') + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="cpu_perf_score">' + tr('cpu_perf_score') + '</div><div class="info-value" id="cpu-score" data-i18n="not_tested">' + tr('not_tested') + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="memory_bandwidth">' + tr('memory_bandwidth') + '</div><div class="info-value" id="mem-speed" data-i18n="not_tested">' + tr('not_tested') + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="memory_stress">' + tr('memory_stress') + '</div><div class="info-value" id="mem-stress" data-i18n="not_tested">' + tr('not_tested') + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="stress_duration">' + tr('stress_duration') + '</div><div class="info-value" id="stress-duration" data-i18n="not_tested">' + tr('not_tested') + '</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="allocations_label">' + tr('allocations_label') + '</div><div class="info-value" id="mem-allocs" data-i18n="not_tested">' + tr('not_tested') + '</div></div>';
    h += '</div></div>';
    return h;
}

function buildExport() {
    let h = '<div class="section"><h2 data-i18n="data_export" data-i18n-prefix="💾">' + tr('data_export') + '</h2><p data-i18n="export_intro">' + tr('export_intro') + '</p>';
    h += '<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px;margin-top:20px">';
    h += '<div class="card" style="text-align:center;padding:30px"><h3 style="color:#667eea" data-i18n="txt_file">' + tr('txt_file') + '</h3><p style="font-size:0.9em;color:#666;margin:15px 0" data-i18n="readable_report">' + tr('readable_report') + '</p><a href="/export/txt" class="btn btn-primary" data-i18n="download_txt" data-i18n-prefix="📥">' + tr('download_txt') + '</a></div>';
    h += '<div class="card" style="text-align:center;padding:30px"><h3 style="color:#3a7bd5" data-i18n="json_file">' + tr('json_file') + '</h3><p style="font-size:0.9em;color:#666;margin:15px 0" data-i18n="structured_format">' + tr('structured_format') + '</p><a href="/export/json" class="btn btn-info" data-i18n="download_json" data-i18n-prefix="📥">' + tr('download_json') + '</a></div>';
    h += '<div class="card" style="text-align:center;padding:30px"><h3 style="color:#56ab2f" data-i18n="csv_file">' + tr('csv_file') + '</h3><p style="font-size:0.9em;color:#666;margin:15px 0" data-i18n="for_excel">' + tr('for_excel') + '</p><a href="/export/csv" class="btn btn-success" data-i18n="download_csv" data-i18n-prefix="📥">' + tr('download_csv') + '</a></div>';
    h += '<div class="card" style="text-align:center;padding:30px"><h3 style="color:#667eea" data-i18n="printable_version">' + tr('printable_version') + '</h3><p style="font-size:0.9em;color:#666;margin:15px 0" data-i18n="pdf_format">' + tr('pdf_format') + '</p><a href="/print" target="_blank" class="btn btn-primary" data-i18n="open" data-i18n-prefix="🖨️">' + tr('open') + '</a></div>';
    h += '</div></div>';
    return h;
}

function buildDisplaySignal(ledsData, screensData) {
    let h = '<div class=\"section\"><p data-i18n=\"display_signal_intro\">' + tr('display_signal_intro') + '</p></div>';
    h += buildLeds(ledsData);
    h += buildScreens(screensData);
    h += '<div class="section"><h2 data-i18n="rgb_led" data-i18n-prefix="💡">' + tr('rgb_led') + '</h2>';
    h += '<p data-i18n="rgb_led_desc">' + tr('rgb_led_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="rgb_led_pins">' + tr('rgb_led_pins') + '</div>';
    h += '<div style="display:flex;gap:5px">';
    h += '<input type="number" id="rgbPinR" value="' + RGB_LED_PIN_R + '" style="width:60px" placeholder="R"/>';
    h += '<input type="number" id="rgbPinG" value="' + RGB_LED_PIN_G + '" style="width:60px" placeholder="G"/>';
    h += '<input type="number" id="rgbPinB" value="' + RGB_LED_PIN_B + '" style="width:60px" placeholder="B"/>';
    h += '<button class="btn btn-info" onclick="applyRGBConfig()" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="testRGBLed()" data-i18n="test_rgb_led" data-i18n-prefix="▶️">' + tr('test_rgb_led') + '</button> ';
    h += '<button class="btn btn-danger" onclick="setRGBColor(255,0,0)" data-i18n="red">' + tr('red') + '</button> ';
    h += '<button class="btn btn-success" onclick="setRGBColor(0,255,0)" data-i18n="green">' + tr('green') + '</button> ';
    h += '<button class="btn btn-info" onclick="setRGBColor(0,0,255)" data-i18n="blue">' + tr('blue') + '</button> ';
    h += '<button class="btn" style="background:#fff;color:#000;border:1px solid #ddd" onclick="setRGBColor(255,255,255)" data-i18n="white">' + tr('white') + '</button> ';
    h += '<button class="btn" style="background:#333" onclick="setRGBColor(0,0,0)" data-i18n="off">' + tr('off') + '</button>';
    h += '</div><div id="rgb-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div></div></div>';
    h += '<div class="section"><h2 data-i18n="buzzer" data-i18n-prefix="🔔">' + tr('buzzer') + '</h2>';
    h += '<p data-i18n="buzzer_desc">' + tr('buzzer_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="buzzer_pin">' + tr('buzzer_pin') + '</div>';
    h += '<div style="display:flex;gap:5px">';
    h += '<input type="number" id="buzzerPin" value="' + BUZZER_PIN + '" style="width:80px"/>';
    h += '<button class="btn btn-info" onclick="applyBuzzerConfig()" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="testBuzzer()" data-i18n="test_buzzer" data-i18n-prefix="▶️">' + tr('test_buzzer') + '</button> ';
    h += '<button class="btn btn-warning" onclick="playTone(1000,300)" data-i18n="beep">' + tr('beep') + '</button>';
    h += '</div><div id="buzzer-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div></div></div>';
    return h;
}

function buildHardwareTests() {
    let h = buildGpio();
    h += buildTests();
    return h;
}

function buildInputDevices() {
    let h = '<div class="section"><h2 data-i18n="input_devices_section" data-i18n-prefix="🎮">' + tr('input_devices_section') + '</h2><p data-i18n="input_devices_intro">' + tr('input_devices_intro') + '</p>';
    h += '<h3 data-i18n="rotary_encoder" data-i18n-prefix="🎚️">' + tr('rotary_encoder') + '</h3>';
    h += '<p data-i18n="rotary_encoder_desc">' + tr('rotary_encoder_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="rotary_pins">' + tr('rotary_pins') + '</div>';
    h += '<div style="display:flex;gap:5px;flex-wrap:wrap">';
    h += '<span data-i18n="rotary_pin_clk">' + tr('rotary_pin_clk') + '</span>: <input type="number" id="rotaryClk" value="' + ROTARY_CLK_PIN + '" min="0" max="48" style="width:70px"/> ';
    h += '<span data-i18n="rotary_pin_dt">' + tr('rotary_pin_dt') + '</span>: <input type="number" id="rotaryDt" value="' + ROTARY_DT_PIN + '" min="0" max="48" style="width:70px"/> ';
    h += '<span data-i18n="rotary_pin_sw">' + tr('rotary_pin_sw') + '</span>: <input type="number" id="rotarySw" value="' + ROTARY_SW_PIN + '" min="0" max="48" style="width:70px"/> ';
    h += '<button class="btn btn-info" onclick="applyRotaryConfig()" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="rotary_position">' + tr('rotary_position') + '</div>';
    h += '<div id="rotary-position" style="font-size:1.5em;font-weight:bold;color:#667eea">0</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="rotary_button">' + tr('rotary_button') + '</div>';
    h += '<div id="rotary-button" style="font-size:1.2em" data-i18n="rotary_button_released">' + tr('rotary_button_released') + '</div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="testRotary()" data-i18n="test_rotary" data-i18n-prefix="▶️">' + tr('test_rotary') + '</button> ';
    h += '<button class="btn btn-info" id="rotary-monitor-btn" onclick="toggleRotaryMonitoring()" data-i18n="rotary_monitor" data-i18n-prefix="👁️">' + tr('rotary_monitor') + '</button> ';
    h += '<button class="btn btn-warning" onclick="resetRotaryPosition()" data-i18n="rotary_reset">' + tr('rotary_reset') + '</button>';
    h += '</div><div id="rotary-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div></div>';
    h += '<h3 data-i18n="button_boot" data-i18n-prefix="🔘">' + tr('button_boot') + '</h3>';
    h += '<p data-i18n="button_boot_desc">' + tr('button_boot_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="button_pin">' + tr('button_pin') + '</div>';
    h += '<div class="info-value">GPIO ' + BUTTON_BOOT + ' <span style="font-size:0.8em;color:#666">(non configurable)</span></div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="button_state">' + tr('button_state') + '</div>';
    h += '<div id="boot-button-state" style="font-size:1.2em;color:#28a745" data-i18n="button_released">' + tr('button_released') + '</div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-info" id="boot-monitor-btn" onclick="toggleBootButtonMonitoring()" data-i18n="monitor_button" data-i18n-prefix="👁️">' + tr('monitor_button') + '</button>';
    h += '</div></div>';
    h += '<h3 data-i18n="button_1" data-i18n-prefix="🔘">' + tr('button_1') + '</h3>';
    h += '<p data-i18n="button_1_desc">' + tr('button_1_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="button_pin">' + tr('button_pin') + '</div>';
    h += '<div style="display:flex;gap:5px">';
    h += '<input type="number" id="button1-pin" value="' + BUTTON_1 + '" min="0" max="48" style="width:70px"/> ';
    h += '<button class="btn btn-info" onclick="applyButtonConfig(\'button1\')" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="button_state">' + tr('button_state') + '</div>';
    h += '<div id="button1-state" style="font-size:1.2em;color:#28a745" data-i18n="button_released">' + tr('button_released') + '</div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-info" id="button1-monitor-btn" onclick="toggleButton1Monitoring()" data-i18n="monitor_button" data-i18n-prefix="👁️">' + tr('monitor_button') + '</button>';
    h += '</div></div>';
    h += '<h3 data-i18n="button_2" data-i18n-prefix="🔘">' + tr('button_2') + '</h3>';
    h += '<p data-i18n="button_2_desc">' + tr('button_2_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="button_pin">' + tr('button_pin') + '</div>';
    h += '<div style="display:flex;gap:5px">';
    h += '<input type="number" id="button2-pin" value="' + BUTTON_2 + '" min="0" max="48" style="width:70px"/> ';
    h += '<button class="btn btn-info" onclick="applyButtonConfig(\'button2\')" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="button_state">' + tr('button_state') + '</div>';
    h += '<div id="button2-state" style="font-size:1.2em;color:#28a745" data-i18n="button_released">' + tr('button_released') + '</div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-info" id="button2-monitor-btn" onclick="toggleButton2Monitoring()" data-i18n="monitor_button" data-i18n-prefix="👁️">' + tr('monitor_button') + '</button>';
    h += '</div></div>';
    h += '</div>';
    return h;
}

function buildMemory() {
    let h = '<div class="section"><h2 data-i18n="memory_section" data-i18n-prefix="💾">' + tr('memory_section') + '</h2><p data-i18n="memory_intro">' + tr('memory_intro') + '</p>';
    h += '<h3 data-i18n="sd_card" data-i18n-prefix="💾">' + tr('sd_card') + '</h3>';
    h += '<p data-i18n="sd_card_desc">' + tr('sd_card_desc') + '</p>';
    h += '<p class="coming" data-i18n="coming_soon">' + tr('coming_soon') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="sd_pins_spi">' + tr('sd_pins_spi') + '</div>';
    h += '<div style="display:flex;gap:5px;flex-wrap:wrap">';
    h += '<span data-i18n="sd_pin_miso">' + tr('sd_pin_miso') + '</span>: <input type="number" id="sdMiso" value="' + SD_MISO_PIN + '" min="0" max="48" style="width:70px"/> ';
    h += '<span data-i18n="sd_pin_mosi">' + tr('sd_pin_mosi') + '</span>: <input type="number" id="sdMosi" value="' + SD_MOSI_PIN + '" min="0" max="48" style="width:70px"/> ';
    h += '<span data-i18n="sd_pin_sclk">' + tr('sd_pin_sclk') + '</span>: <input type="number" id="sdSclk" value="' + SD_SCLK_PIN + '" min="0" max="48" style="width:70px"/> ';
    h += '<span data-i18n="sd_pin_cs">' + tr('sd_pin_cs') + '</span>: <input type="number" id="sdCs" value="' + SD_CS_PIN + '" min="0" max="48" style="width:70px"/> ';
    h += '<button class="btn btn-info" onclick="applySDConfig()" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div></div>';
    h += '<p style="margin-top:10px;padding:10px;background:#fff3cd;border-left:4px solid #ffc107;color:#856404;border-radius:4px"><strong>⚠️ ' + tr('gpio_shared_warning') + '</strong><br>' + tr('gpio_13_shared_desc') + '</p>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="testSD()" data-i18n="test_sd" data-i18n-prefix="▶️">' + tr('test_sd') + '</button> ';
    h += '<button class="btn btn-success" onclick="testSDRead()" data-i18n="sd_test_read" data-i18n-prefix="📖">' + tr('sd_test_read') + '</button> ';
    h += '<button class="btn btn-warning" onclick="testSDWrite()" data-i18n="sd_test_write" data-i18n-prefix="✍️">' + tr('sd_test_write') + '</button> ';
    h += '<button class="btn btn-danger" onclick="formatSD()" data-i18n="sd_format" data-i18n-prefix="⚠️">' + tr('sd_format') + '</button> ';
    h += '<button class="btn btn-info" onclick="loadSDInfo()" data-i18n="refresh" data-i18n-prefix="🔄">' + tr('refresh') + '</button>';
    h += '</div><div id="sd-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div>';
    h += '<div id="sd-results" class="info-grid"></div></div>';
    h += '</div>';
    return h;
}

function buildSensors() {
    let h = '<div class="section"><h2 data-i18n="sensors_section" data-i18n-prefix="📡">' + tr('sensors_section') + '</h2><p data-i18n="sensors_intro">' + tr('sensors_intro') + '</p>';
    h += '<h3 data-i18n="dht_sensor" data-i18n-prefix="🌡️">' + tr('dht_sensor') + '</h3>';
    h += '<p data-i18n="dht_sensor_desc">' + tr('dht_sensor_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="dht_sensor_pin">' + tr('dht_sensor_pin') + '</div>';
    h += '<div style="display:flex;gap:5px">';
    h += '<input type="number" id="dhtPin" value="' + DHT_PIN + '" style="width:80px"/>';
    h += '<button class="btn btn-info" onclick="applyDHTConfig()" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="dht_sensor_type">' + tr('dht_sensor_type') + '</div>';
    h += '<div><select id="dhtSensorType" style="min-width:140px">';
    h += '<option value="22" data-i18n="dht11_option">' + tr('dht11_option') + '</option>';
    h += '<option value="22" data-i18n="dht22_option">' + tr('dht22_option') + '</option></select></div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="testDHTSensor()" data-i18n="test_dht_sensor" data-i18n-prefix="▶️">' + tr('test_dht_sensor') + '</button>';
    h += '</div><div id="dht-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div>';
    h += '<div id="dht-results" class="info-grid"></div></div>';
    h += '<h3 data-i18n="environmental_sensors" data-i18n-prefix="🌦️">' + tr('environmental_sensors') + '</h3>';
    h += '<p data-i18n="environmental_sensors_desc">' + tr('environmental_sensors_desc') + '</p>';
    h += '<div class="card"><div class="info-grid" id="env-info">';
    h += '<div class="info-item"><div class="info-label" data-i18n="aht20_sensor">' + tr('aht20_sensor') + '</div><div class="info-value" id="env-aht20-status">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="bmp280_sensor">' + tr('bmp280_sensor') + '</div><div class="info-value" id="env-bmp280-status">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="temperature_avg">' + tr('temperature_avg') + '</div><div class="info-value" id="env-temp-avg">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="humidity">' + tr('humidity') + '</div><div class="info-value" id="env-humidity">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="pressure_hpa">' + tr('pressure_hpa') + '</div><div class="info-value" id="env-pressure">-</div></div>';
    h += '<div class="info-item"><div class="info-label" data-i18n="altitude_calculated">' + tr('altitude_calculated') + '</div><div class="info-value" id="env-altitude">-</div></div>';
    h += '</div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="loadEnvironmentalData()" data-i18n="refresh_env_sensors" data-i18n-prefix="🔄">' + tr('refresh_env_sensors') + '</button> ';
    h += '<button class="btn btn-info" onclick="testEnvironmentalSensors()" data-i18n="test_env_sensors" data-i18n-prefix="🧪">' + tr('test_env_sensors') + '</button>';
    h += '</div><div id="env-status" class="status-live"></div></div>';
    h += '<h3 data-i18n="light_sensor" data-i18n-prefix="☀️">' + tr('light_sensor') + '</h3>';
    h += '<p data-i18n="light_sensor_desc">' + tr('light_sensor_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="light_sensor_pin">' + tr('light_sensor_pin') + '</div>';
    h += '<div style="display:flex;gap:5px">';
    h += '<input type="number" id="lightPin" value="' + LIGHT_SENSOR_PIN + '" style="width:80px"/>';
    h += '<button class="btn btn-info" onclick="applyLightConfig()" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="testLightSensor()" data-i18n="test_light_sensor" data-i18n-prefix="▶️">' + tr('test_light_sensor') + '</button>';
    h += '</div><div id="light-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div>';
    h += '<div id="light-results" class="info-grid"></div></div>';
    h += '<h3 data-i18n="distance_sensor" data-i18n-prefix="📏">' + tr('distance_sensor') + '</h3>';
    h += '<p data-i18n="distance_sensor_desc">' + tr('distance_sensor_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="distance_pins">' + tr('distance_pins') + '</div>';
    h += '<div style="display:flex;gap:5px">';
    h += '<input type="number" id="distTrig" value="' + DISTANCE_TRIG_PIN + '" style="width:60px" data-i18n-placeholder="label_trig" placeholder="' + tr('label_trig') + '"/>';
    h += '<input type="number" id="distEcho" value="' + DISTANCE_ECHO_PIN + '" style="width:60px" data-i18n-placeholder="label_echo" placeholder="' + tr('label_echo') + '"/>';
    h += '<button class="btn btn-info" onclick="applyDistanceConfig()" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="testDistanceSensor()" data-i18n="test_distance_sensor" data-i18n-prefix="▶️">' + tr('test_distance_sensor') + '</button>';
    h += '</div><div id="distance-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div>';
    h += '<div id="distance-results" class="info-grid"></div></div>';
    h += '<h3 data-i18n="motion_sensor" data-i18n-prefix="👁️">' + tr('motion_sensor') + '</h3>';
    h += '<p data-i18n="motion_sensor_desc">' + tr('motion_sensor_desc') + '</p>';
    h += '<div class="card"><div class="info-grid">';
    h += '<div class="info-item"><div class="info-label" data-i18n="motion_sensor_pin">' + tr('motion_sensor_pin') + '</div>';
    h += '<div style="display:flex;gap:5px">';
    h += '<input type="number" id="motionPin" value="' + MOTION_SENSOR_PIN + '" style="width:80px"/>';
    h += '<button class="btn btn-info" onclick="applyMotionConfig()" data-i18n="apply_config">' + tr('apply_config') + '</button></div></div></div>';
    h += '<div style="text-align:center;margin:15px 0">';
    h += '<button class="btn btn-primary" onclick="testMotionSensor()" data-i18n="test_motion_sensor" data-i18n-prefix="▶️">' + tr('test_motion_sensor') + '</button>';
    h += '</div><div id="motion-status" class="status-live" data-i18n="click_to_test">' + tr('click_to_test') + '</div>';
    h += '<div id="motion-results" class="info-grid"></div></div>';
    h += '</div>';
    return h;
}
async function testBuiltinLED() {
    setStatus('builtin-led-status', {
        key: 'test_in_progress'
    }, null);
    const r = await fetch('/api/builtin-led-test');
    const d = await r.json();
    setStatus('builtin-led-status', d.result, d.success ? 'success' : 'error');
}
async function ledBlink() {
    setStatus('builtin-led-status', {
        key: 'transmission'
    }, null);
    const r = await fetch('/api/builtin-led-control?action=blink');
    const d = await r.json();
    setStatus('builtin-led-status', d.message, null);
}
async function ledFade() {
    setStatus('builtin-led-status', {
        key: 'transmission'
    }, null);
    const r = await fetch('/api/builtin-led-control?action=fade');
    const d = await r.json();
    setStatus('builtin-led-status', d.message, null);
}
async function ledOff() {
    setStatus('builtin-led-status', {
        key: 'transmission'
    }, null);
    const r = await fetch('/api/builtin-led-control?action=off');
    const d = await r.json();
    setStatus('builtin-led-status', d.message, null);
}
async function ledOn() {
    setStatus('builtin-led-status', {
        key: 'transmission'
    }, null);
    const r = await fetch('/api/builtin-led-control?action=on');
    const d = await r.json();
    setStatus('builtin-led-status', d.message, null);
}
async function configBuiltinLED() {
    const gpio = document.getElementById('builtin-led-gpio').value;
    setStatus('builtin-led-status', {
        key: 'transmission'
    }, null);
    const r = await fetch('/api/builtin-led-config?gpio=' + gpio);
    const d = await r.json();
    setStatus('builtin-led-status', d.message, d.success ? 'success' : 'error');
    if (d.success) {
        // Update GPIO display dynamically without page reload (v3.33.4)
        const gpioDisplay = document.getElementById('builtin-gpio-display');
        if (gpioDisplay) {
            gpioDisplay.textContent = gpio;
        }
    }
}
async function neoCustomColor() {
    const color = document.getElementById('neoColor').value;
    const r = parseInt(color.substr(1, 2), 16);
    const g = parseInt(color.substr(3, 2), 16);
    const b = parseInt(color.substr(5, 2), 16);
    setStatus('neopixel-status', {
        key: 'transmission',
        suffix: ' RGB(' + r + ',' + g + ',' + b + ')'
    }, null);
    const resp = await fetch('/api/neopixel-color?r=' + r + '&g=' + g + '&b=' + b);
    const d = await resp.json();
    setStatus('neopixel-status', d.message, null);
}
async function testNeoPixel() {
    setStatus('neopixel-status', {
        key: 'test_in_progress'
    }, null);
    const r = await fetch('/api/neopixel-test');
    const d = await r.json();
    setStatus('neopixel-status', d.result, null);
}
async function neoPattern(p) {
    setStatus('neopixel-status', {
        key: 'transmission'
    }, null);
    const r = await fetch('/api/neopixel-pattern?pattern=' + p);
    const d = await r.json();
    setStatus('neopixel-status', d.message, null);
}
async function configNeoPixel() {
    const gpio = document.getElementById('neopixel-gpio').value;
    const count = document.getElementById('neopixel-count').value;
    setStatus('neopixel-status', {
        key: 'transmission'
    }, null);
    const r = await fetch('/api/neopixel-config?gpio=' + gpio + '&count=' + count);
    const d = await r.json();
    setStatus('neopixel-status', d.message, d.success ? 'success' : 'error');
    if (d.success) {
        // Update GPIO and count display dynamically without page reload (v3.33.4)
        const gpioDisplay = document.getElementById('neopixel-gpio-display');
        if (gpioDisplay) {
            gpioDisplay.textContent = gpio;
        }
        const countDisplay = document.getElementById('neopixel-count-display');
        if (countDisplay) {
            countDisplay.textContent = count;
        }
    }
}
async function testOLED() {
    setStatus('oled-status', {
        key: 'oled_test_running'
    }, null);
    const r = await fetch('/api/oled-test');
    const d = await r.json();
    setStatus('oled-status', d.result, null);
}
async function oledStep(step) {
    setStatus('oled-status', {
        key: 'oled_step_running'
    }, null);
    try {
        const r = await fetch('/api/oled-step?step=' + step);
        const d = await r.json();
        setStatus('oled-status', d.message, d.success ? 'success' : 'error');
    } catch (e) {
        setStatus('oled-status', {
            key: 'error_label',
            suffix: ': ' + String(e)
        }, 'error');
    }
}
async function oledDisplayText() {
    const text = document.getElementById('oledText').value;
    if (!text) {
        setStatus('oled-status', {
            key: 'oled_message_required'
        }, 'error');
        return;
    }
    setStatus('oled-status', {
        key: 'oled_displaying_message'
    }, null);
    try {
        const r = await fetch('/api/oled-message?message=' + encodeURIComponent(text));
        const d = await r.json();
        setStatus('oled-status', d.message, d.success ? 'success' : 'error');
    } catch (e) {
        setStatus('oled-status', {
            key: 'error_label',
            suffix: ': ' + String(e)
        }, 'error');
    }
}
async function oledBoot() {
    setStatus('oled-status', {
        key: 'restoring_boot_screen'
    }, null);
    try {
        const r = await fetch('/api/oled-boot');
        const d = await r.json();
        setStatus('oled-status', d.message, d.success ? 'success' : 'error');
    } catch (e) {
        setStatus('oled-status', {
            key: 'error_label',
            suffix: ': ' + String(e)
        }, 'error');
    }
}
async function configOLED() {
    setStatus('oled-status', {
        key: 'reconfiguring'
    }, null);
    const sda = document.getElementById('oledSDA').value;
    const scl = document.getElementById('oledSCL').value;
    const rotation = document.getElementById('oledRotation').value;
    const width = document.getElementById('oledWidth').value;
    const height = document.getElementById('oledHeight').value;
    try {
        const r = await fetch('/api/oled-config?sda=' + sda + '&scl=' + scl + '&rotation=' + rotation + '&width=' + width + '&height=' + height);
        const d = await r.json();
        const statusPayload = d.message ? {
            text: d.message
        } : {
            key: 'configuration_invalid'
        };
        setStatus('oled-status', statusPayload, d.success ? 'success' : 'error');
        if (d.success) {
            if (typeof d.sda !== 'undefined') {
                document.getElementById('oledSDA').value = d.sda;
                document.getElementById('oledSCL').value = d.scl;
            }
            const pins = document.getElementById('oled-pins');
            if (pins) {
                pins.innerHTML = '<span data-i18n="label_sda" data-i18n-suffix=" :">' + tr('label_sda') + '</span>' + d.sda + ' <span data-i18n="label_scl" data-i18n-suffix=" :">' + tr('label_scl') + '</span>' + d.scl;
            }
            const rotDisplay = document.getElementById('oled-rotation-display');
            if (rotDisplay) {
                rotDisplay.textContent = d.rotation;
            }
            if (document.getElementById('oledRotation')) {
                document.getElementById('oledRotation').value = d.rotation;
            }
        }
    } catch (e) {
        setStatus('oled-status', {
            key: 'error_label',
            suffix: ': ' + String(e)
        }, 'error');
    }
}
async function testTFT() {
    setStatus('tft-status', {
        key: 'tft_test_running'
    }, null);
    const r = await fetch('/api/tft-test');
    const d = await r.json();
    setStatus('tft-status', d.result, null);
}
async function tftStep(step) {
    setStatus('tft-status', {
        key: 'tft_step_running'
    }, null);
    try {
        const r = await fetch('/api/tft-step?step=' + step);
        const d = await r.json();
        setStatus('tft-status', d.message, d.success ? 'success' : 'error');
    } catch (e) {
        setStatus('tft-status', {
            key: 'error_label',
            suffix: ': ' + String(e)
        }, 'error');
    }
}
async function tftBoot() {
    setStatus('tft-status', {
        key: 'restoring_boot_screen'
    }, null);
    try {
        const r = await fetch('/api/tft-boot');
        const d = await r.json();
        setStatus('tft-status', d.message, d.success ? 'success' : 'error');
    } catch (e) {
        setStatus('tft-status', {
            key: 'error_label',
            suffix: ': ' + String(e)
        }, 'error');
    }
}
async function configTFT() {
    setStatus('tft-status', {
        key: 'reconfiguring'
    }, null);
    const miso = document.getElementById('tftMISO').value;
    const mosi = document.getElementById('tftMOSI').value;
    const sclk = document.getElementById('tftSCLK').value;
    const cs = document.getElementById('tftCS').value;
    const dc = document.getElementById('tftDC').value;
    const rst = document.getElementById('tftRST').value;
    const bl = document.getElementById('tftBL').value;
    const width = document.getElementById('tftWidth').value;
    const height = document.getElementById('tftHeight').value;
    const rotation = document.getElementById('tftRotation').value;
    const driver = document.getElementById('tftDriver').value;
    try {
        const r = await fetch('/api/tft-config?driver=' + driver + '&miso=' + miso + '&mosi=' + mosi + '&sclk=' + sclk + '&cs=' + cs + '&dc=' + dc + '&rst=' + rst + '&bl=' + bl + '&width=' + width + '&height=' + height + '&rotation=' + rotation);
        const d = await r.json();
        const statusPayload = d.message ? {
            text: d.message
        } : {
            key: 'configuration_invalid'
        };
        setStatus('tft-status', statusPayload, d.success ? 'success' : 'error');
        if (d.success) {
            // Update TFT configuration display dynamically without page reload (v3.33.4)
            const pinsDisplay = document.getElementById('tft-pins-display');
            if (pinsDisplay) {
                pinsDisplay.textContent = 'MISO:' + miso + ' MOSI:' + mosi + ' SCLK:' + sclk + ' CS:' + cs + ' DC:' + dc + ' RST:' + rst;
            }
            const resolutionDisplay = document.getElementById('tft-resolution');
            if (resolutionDisplay) {
                resolutionDisplay.textContent = width + ' x ' + height;
            }
        }
    } catch (e) {
        setStatus('tft-status', {
            key: 'error_label',
            suffix: ': ' + String(e)
        }, 'error');
    }
}

// ============================================================
// TFT BRIGHTNESS CONTROL FUNCTIONS (v3.33.3)
// ============================================================

// Update brightness value display when slider moves
function updateBrightnessValue(value) {
    document.getElementById('tftBrightnessValue').textContent = value;
}

// Set TFT brightness to specific level
async function setTFTBrightnessLevel(level) {
    const slider = document.getElementById('tftBrightnessSlider');
    const valueDisplay = document.getElementById('tftBrightnessValue');

    if (slider) slider.value = level;
    if (valueDisplay) valueDisplay.textContent = level;

    try {
        const r = await fetch('/api/tft-brightness?value=' + level, {
            method: 'POST'
        });
        const d = await r.json();

        if (d.success) {
            console.log('TFT brightness set to ' + level + '/255');
        } else {
            console.error('Failed to set TFT brightness: ' + d.message);
        }
    } catch (e) {
        console.error('Error setting TFT brightness: ' + e);
    }
}

// Get current TFT brightness from API
async function getTFTBrightness() {
    try {
        const r = await fetch('/api/tft-brightness');
        const d = await r.json();

        if (d.success && d.brightness !== undefined) {
            const slider = document.getElementById('tftBrightnessSlider');
            const valueDisplay = document.getElementById('tftBrightnessValue');

            if (slider) slider.value = d.brightness;
            if (valueDisplay) valueDisplay.textContent = d.brightness;
        }
    } catch (e) {
        console.error('Error getting TFT brightness: ' + e);
    }
}

async function testADC() {
    setStatus('adc-status', {
        key: 'test_in_progress'
    }, null);
    const r = await fetch('/api/adc-test');
    const d = await r.json();
    let h = '';
    d.readings.forEach(rd => {
        h += '<div class="info-item"><div class="info-label">GPIO ' + rd.pin + '</div><div class="info-value">' + rd.raw + ' (' + rd.voltage.toFixed(2) + 'V)</div></div>';
    });
    document.getElementById('adc-results').innerHTML = h;
    setStatus('adc-status', d.result, null);
}
async function testAllGPIO() {
    setStatus('gpio-status', {
        key: 'test_in_progress'
    }, null);
    const r = await fetch('/api/test-gpio');
    const d = await r.json();
    let h = '';
    d.results.forEach(g => {
        h += '<div class="gpio-item ' + (g.working ? 'gpio-ok' : 'gpio-fail') + '">GPIO ' + g.pin + '<br>' + (g.working ? '✅ OK' : '❌ FAIL') + '</div>';
    });
    document.getElementById('gpio-results').innerHTML = h;
    setStatus('gpio-status', {
        key: 'gpio_test_complete',
        replacements: {
            count: d.results.length
        }
    }, null);
}
async function scanWiFi() {
    setStatus('wifi-status', {
        key: 'wifi_scan_in_progress'
    }, null);
    const r = await fetch('/api/wifi-scan');
    const d = await r.json();
    let h = '';
    d.networks.forEach(n => {
        const icon = n.rssi >= -60 ? '🟢' : n.rssi >= -70 ? '🟡' : '🔴';
        const color = n.rssi >= -60 ? '#28a745' : n.rssi >= -70 ? '#ffc107' : '#dc3545';
        h += '<div class="wifi-item"><div style="display:flex;justify-content:space-between"><div><strong>' + icon + ' ' + n.ssid + '</strong><br><small>' + n.bssid + ' | ' + tr('wifi_channel') + ' ' + n.channel + '</small></div>';
        h += '<div style="font-size:1.3em;font-weight:bold;color:' + color + '">' + n.rssi + ' dBm</div></div></div>';
    });
    document.getElementById('wifi-results').innerHTML = h;
    setStatus('wifi-status', {
        key: 'wifi_networks_found',
        replacements: {
            count: d.networks.length
        }
    }, null);
}
async function loadWirelessInfo() {
    try {
        const r = await fetch('/api/wifi-info');
        const d = await r.json();
        const connectedNode = document.getElementById('wifi-connected');
        const ssidNode = document.getElementById('wifi-current-ssid');
        const ipNode = document.getElementById('wifi-ip');
        const gatewayNode = document.getElementById('wifi-gateway');
        const dnsNode = document.getElementById('wifi-dns');
        const rssiNode = document.getElementById('wifi-rssi');
        if (connectedNode) {
            clearTranslationAttributes(connectedNode);
            connectedNode.textContent = d.connected ? tr('connected') : tr('disconnected');
            connectedNode.style.color = d.connected ? '#28a745' : '#dc3545';
        }
        if (ssidNode) {
            clearTranslationAttributes(ssidNode);
            ssidNode.textContent = d.ssid || '-';
        }
        if (ipNode) {
            clearTranslationAttributes(ipNode);
            ipNode.textContent = d.ip || '-';
        }
        if (gatewayNode) {
            clearTranslationAttributes(gatewayNode);
            gatewayNode.textContent = d.gateway || '-';
        }
        if (dnsNode) {
            clearTranslationAttributes(dnsNode);
            dnsNode.textContent = d.dns || '-';
        }
        if (rssiNode) {
            clearTranslationAttributes(rssiNode);
            rssiNode.textContent = d.connected ? (d.rssi + ' dBm') : '-';
        }
    } catch (e) {
        console.error('Error loading WiFi info:', e);
    }
    loadGPSData();
}
async function loadGPSData() {
    try {
        const r = await fetch('/api/gps');
        const d = await r.json();
        const statusNode = document.getElementById('gps-status-value');
        const latNode = document.getElementById('gps-latitude');
        const lonNode = document.getElementById('gps-longitude');
        const altNode = document.getElementById('gps-altitude');
        const satNode = document.getElementById('gps-satellites');
        const hdopNode = document.getElementById('gps-hdop');
        if (statusNode) {
            clearTranslationAttributes(statusNode);
            statusNode.textContent = d.status || '-';
            statusNode.style.color = d.status === 'Fix OK' ? '#28a745' : '#dc3545';
        }
        if (latNode) {
            clearTranslationAttributes(latNode);
            latNode.textContent = d.latitude ? d.latitude.toFixed(6) + '°' : '-';
        }
        if (lonNode) {
            clearTranslationAttributes(lonNode);
            lonNode.textContent = d.longitude ? d.longitude.toFixed(6) + '°' : '-';
        }
        if (altNode) {
            clearTranslationAttributes(altNode);
            altNode.textContent = d.altitude ? d.altitude.toFixed(1) + ' m' : '-';
        }
        if (satNode) {
            clearTranslationAttributes(satNode);
            satNode.textContent = d.satellites || '0';
        }
        if (hdopNode) {
            clearTranslationAttributes(hdopNode);
            hdopNode.textContent = d.hdop ? d.hdop.toFixed(2) : '-';
        }
    } catch (e) {
        console.error('Error loading GPS data:', e);
    }
}
async function testGPS() {
    setStatus('gps-test-status', {
        key: 'test_in_progress'
    }, null);
    try {
        const r = await fetch('/api/gps-test');
        const d = await r.json();
        setStatus('gps-test-status', {
            text: d.result || 'Test complete'
        }, d.success ? 'success' : 'error');
        setTimeout(() => loadGPSData(), 1000);
    } catch (e) {
        setStatus('gps-test-status', {
            key: 'error_label'
        }, 'error');
    }
}
async function loadEnvironmentalData() {
    try {
        const r = await fetch('/api/environmental-sensors');
        const d = await r.json();
        const aht20Node = document.getElementById('env-aht20-status');
        const bmp280Node = document.getElementById('env-bmp280-status');
        const tempNode = document.getElementById('env-temp-avg');
        const humNode = document.getElementById('env-humidity');
        const pressNode = document.getElementById('env-pressure');
        const altNode = document.getElementById('env-altitude');
        if (aht20Node) {
            clearTranslationAttributes(aht20Node);
            aht20Node.textContent = d.aht20_available ? '✅ ' + tr('available') : '❌ ' + tr('not_available');
            aht20Node.style.color = d.aht20_available ? '#28a745' : '#dc3545';
        }
        if (bmp280Node) {
            clearTranslationAttributes(bmp280Node);
            bmp280Node.textContent = d.bmp280_available ? '✅ ' + tr('available') : '❌ ' + tr('not_available');
            bmp280Node.style.color = d.bmp280_available ? '#28a745' : '#dc3545';
        }
        if (tempNode) {
            clearTranslationAttributes(tempNode);
            tempNode.textContent = d.temperature_avg ? d.temperature_avg.toFixed(1) + ' °C' : '-';
        }
        if (humNode) {
            clearTranslationAttributes(humNode);
            humNode.textContent = d.humidity ? d.humidity.toFixed(1) + ' %' : '-';
        }
        if (pressNode) {
            clearTranslationAttributes(pressNode);
            pressNode.textContent = d.pressure ? d.pressure.toFixed(1) + ' hPa' : '-';
        }
        if (altNode) {
            clearTranslationAttributes(altNode);
            altNode.textContent = d.altitude ? d.altitude.toFixed(1) + ' m' : '-';
        }
    } catch (e) {
        console.error('Error loading environmental data:', e);
    }
}
async function testEnvironmentalSensors() {
    setStatus('env-status', {
        key: 'test_in_progress'
    }, null);
    try {
        const r = await fetch('/api/environmental-test');
        const d = await r.json();
        setStatus('env-status', {
            text: d.result || 'Test complete'
        }, d.success ? 'success' : 'error');
        setTimeout(() => loadEnvironmentalData(), 1000);
    } catch (e) {
        setStatus('env-status', {
            key: 'error_label'
        }, 'error');
    }
}
async function runBenchmarks() {
    const cpuNode = document.getElementById('cpu-bench');
    const memNode = document.getElementById('mem-bench');
    const cpuScoreNode = document.getElementById('cpu-score');
    const memSpeedNode = document.getElementById('mem-speed');
    const stressNode = document.getElementById('mem-stress');
    const durationNode = document.getElementById('stress-duration');
    const allocNode = document.getElementById('mem-allocs');
    if (cpuNode) setElementTranslation(cpuNode, {
        key: 'test_in_progress'
    });
    if (memNode) setElementTranslation(memNode, {
        key: 'test_in_progress'
    });
    if (cpuScoreNode) setElementTranslation(cpuScoreNode, {
        key: 'test_in_progress'
    });
    if (memSpeedNode) setElementTranslation(memSpeedNode, {
        key: 'test_in_progress'
    });
    if (stressNode) setElementTranslation(stressNode, {
        key: 'test_in_progress'
    });
    if (durationNode) setElementTranslation(durationNode, {
        key: 'test_in_progress'
    });
    if (allocNode) setElementTranslation(allocNode, {
        key: 'test_in_progress'
    });
    try {
        const r = await fetch('/api/benchmark');
        const d = await r.json();
        if (cpuNode) {
            if (typeof d.cpu === 'number' && isFinite(d.cpu)) {
                clearTranslationAttributes(cpuNode);
                cpuNode.textContent = d.cpu + ' µs';
            } else {
                setElementTranslation(cpuNode, {
                    key: 'not_available'
                });
            }
        }
        if (memNode) {
            if (typeof d.memory === 'number' && isFinite(d.memory)) {
                clearTranslationAttributes(memNode);
                memNode.textContent = d.memory + ' µs';
            } else {
                setElementTranslation(memNode, {
                    key: 'not_available'
                });
            }
        }
        if (cpuScoreNode) {
            if (typeof d.cpuPerf === 'number' && isFinite(d.cpuPerf)) {
                clearTranslationAttributes(cpuScoreNode);
                cpuScoreNode.textContent = d.cpuPerf.toFixed(2) + ' ops/µs';
            } else {
                setElementTranslation(cpuScoreNode, {
                    key: 'not_available'
                });
            }
        }
        if (memSpeedNode) {
            if (typeof d.memSpeed === 'number' && isFinite(d.memSpeed)) {
                clearTranslationAttributes(memSpeedNode);
                const bandwidth = d.memSpeed * 0.95367431640625;
                memSpeedNode.textContent = bandwidth.toFixed(2) + ' MB/s';
            } else {
                setElementTranslation(memSpeedNode, {
                    key: 'not_available'
                });
            }
        }
        if (stressNode) {
            if (d.stress && typeof d.stress === 'string') {
                clearTranslationAttributes(stressNode);
                stressNode.textContent = d.stress;
            } else {
                setElementTranslation(stressNode, {
                    key: 'not_available'
                });
            }
        }
        if (durationNode) {
            if (typeof d.stressDuration === 'number' && isFinite(d.stressDuration)) {
                clearTranslationAttributes(durationNode);
                durationNode.textContent = (d.stressDuration / 1000).toFixed(2) + ' s';
            } else {
                setElementTranslation(durationNode, {
                    key: 'not_available'
                });
            }
        }
        if (allocNode) {
            if (typeof d.allocations === 'number' && isFinite(d.allocations)) {
                clearTranslationAttributes(allocNode);
                allocNode.textContent = d.allocations + ' KB';
            } else if (d.allocationsLabel && typeof d.allocationsLabel === 'string') {
                clearTranslationAttributes(allocNode);
                allocNode.textContent = d.allocationsLabel;
            } else {
                setElementTranslation(allocNode, {
                    key: 'not_available'
                });
            }
        }
    } catch (e) {
        if (cpuNode) setElementTranslation(cpuNode, {
            key: 'error_label'
        });
        if (memNode) setElementTranslation(memNode, {
            key: 'error_label'
        });
        if (cpuScoreNode) setElementTranslation(cpuScoreNode, {
            key: 'error_label'
        });
        if (memSpeedNode) setElementTranslation(memSpeedNode, {
            key: 'error_label'
        });
        if (stressNode) setElementTranslation(stressNode, {
            key: 'error_label'
        });
        if (durationNode) setElementTranslation(durationNode, {
            key: 'error_label'
        });
        if (allocNode) setElementTranslation(allocNode, {
            key: 'error_label'
        });
    }
}
async function runPWMTest() {
    setStatus('pwm-status', {
        key: 'test_in_progress'
    }, null);
    try {
        const r = await fetch('/api/pwm-test');
        const d = await r.json();
        const message = d && typeof d.result === 'string' ? d.result : tr('error_label');
        const ok = message.toLowerCase().includes('ok');
        setStatus('pwm-status', {
            text: message
        }, ok ? 'success' : 'error');
    } catch (e) {
        setStatus('pwm-status', {
            key: 'error_label'
        }, 'error');
    }
}
async function runSPIScan() {
    setStatus('spi-status', {
        key: 'test_in_progress'
    }, null);
    const container = document.getElementById('spi-results');
    if (container) {
        container.innerHTML = '';
    }
    try {
        const r = await fetch('/api/spi-scan');
        const d = await r.json();
        if (container) {
            const item = document.createElement('div');
            item.className = 'info-item';
            item.textContent = d && typeof d.info === 'string' && d.info.length > 0 ? d.info : tr('not_available');
            container.appendChild(item);
        }
        const hasInfo = d && typeof d.info === 'string' && d.info.length > 0;
        if (hasInfo) {
            setStatus('spi-status', {
                key: 'ok'
            }, 'success');
        } else {
            setStatus('spi-status', {
                key: 'not_available'
            }, 'error');
        }
    } catch (e) {
        if (container) {
            container.innerHTML = '';
        }
        setStatus('spi-status', {
            key: 'error_label'
        }, 'error');
    }
}
async function runStressTest() {
    setStatus('stress-status', {
        key: 'stress_running'
    }, null);
    const container = document.getElementById('stress-results');
    if (container) {
        container.innerHTML = '<div class="loading"></div>';
    }
    const safeText = value => String(value ?? '').replace(/[&<>"']/g, c => ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        '\'': '&#39;'
    } [c]));
    try {
        const r = await fetch('/api/stress-test');
        const d = await r.json();
        if (container) {
            let h = '';
            if (d.result && typeof d.result === 'string') {
                h += '<div class="info-item"><div class="info-label" data-i18n="memory_stress">' + tr('memory_stress') + '</div><div class="info-value">' + safeText(d.result) + '</div></div>';
            }
            if (typeof d.allocations === 'number' && isFinite(d.allocations)) {
                h += '<div class="info-item"><div class="info-label" data-i18n="allocations_label">' + tr('allocations_label') + '</div><div class="info-value">' + safeText(d.allocations) + ' KB</div></div>';
            } else if (d.allocationsLabel && typeof d.allocationsLabel === 'string') {
                h += '<div class="info-item"><div class="info-label" data-i18n="allocations_label">' + tr('allocations_label') + '</div><div class="info-value">' + safeText(d.allocationsLabel) + '</div></div>';
            }
            if (typeof d.durationMs === 'number' && isFinite(d.durationMs)) {
                h += '<div class="info-item"><div class="info-label" data-i18n="stress_duration">' + tr('stress_duration') + '</div><div class="info-value">' + (d.durationMs / 1000).toFixed(2) + ' s</div></div>';
            }
            if (!h) {
                h = '<div class="info-item"><div class="info-value" data-i18n="not_available">' + tr('not_available') + '</div></div>';
            }
            container.innerHTML = h;
            updateInterfaceTexts();
        }
        const suffix = d.result && typeof d.result === 'string' ? ' ' + d.result : '';
        setStatus('stress-status', {
            key: 'stress_completed',
            prefix: '✅ ',
            suffix: suffix
        }, 'success');
    } catch (e) {
        if (container) {
            container.innerHTML = '';
        }
        setStatus('stress-status', {
            key: 'error_label',
            prefix: '❌ ',
            suffix: ': ' + String(e)
        }, 'error');
    }
}
async function applyRGBConfig() {
    const r = parseInt(document.getElementById('rgbPinR').value);
    const g = parseInt(document.getElementById('rgbPinG').value);
    const b = parseInt(document.getElementById('rgbPinB').value);
    const resp = await fetch('/api/rgb-led-config?r=' + r + '&g=' + g + '&b=' + b);
    const d = await resp.json();
    setStatus('rgb-status', d.message, d.success ? 'success' : 'error');
}
async function testRGBLed() {
    setStatus('rgb-status', {
        key: 'test_in_progress'
    }, null);
    const r = await fetch('/api/rgb-led-test');
    const d = await r.json();
    setStatus('rgb-status', d.result, d.success ? 'success' : 'error');
}
async function setRGBColor(r, g, b) {
    setStatus('rgb-status', 'RGB(' + r + ',' + g + ',' + b + ')', null);
    const resp = await fetch('/api/rgb-led-color?r=' + r + '&g=' + g + '&b=' + b);
    const d = await resp.json();
    setStatus('rgb-status', d.message, d.success ? 'success' : 'error');
}
async function applyBuzzerConfig() {
    const pin = parseInt(document.getElementById('buzzerPin').value);
    const resp = await fetch('/api/buzzer-config?pin=' + pin);
    const d = await resp.json();
    setStatus('buzzer-status', d.message, d.success ? 'success' : 'error');
}
async function testBuzzer() {
    setStatus('buzzer-status', {
        key: 'test_in_progress'
    }, null);
    const r = await fetch('/api/buzzer-test');
    const d = await r.json();
    setStatus('buzzer-status', d.result, d.success ? 'success' : 'error');
}
async function playTone(freq, duration) {
    setStatus('buzzer-status', freq + ' Hz', null);
    const resp = await fetch('/api/buzzer-tone?freq=' + freq + '&duration=' + duration);
    const d = await resp.json();
    setStatus('buzzer-status', d.message, d.success ? 'success' : 'error');
}
async function applyDHTConfig() {
    const pin = parseInt(document.getElementById('dhtPin').value);
    const type = document.getElementById('dhtSensorType').value;
    const params = new URLSearchParams();
    if (!Number.isNaN(pin)) {
        params.append('pin', pin);
    }
    if (type) {
        params.append('type', type);
    }
    const query = params.toString();
    const resp = await fetch('/api/dht-config' + (query.length ? '?' + query : ''));
    const d = await resp.json();
    if (d.type !== undefined) {
        document.getElementById('dhtSensorType').value = String(d.type);
    }
    setStatus('dht-status', d.message, d.success ? 'success' : 'error');
}
async function testDHTSensor() {
    setStatus('dht-status', {
        key: 'test_in_progress'
    }, null);
    document.getElementById('dht-results').innerHTML = '';
    const r = await fetch('/api/dht-test');
    const d = await r.json();
    if (d.type !== undefined) {
        document.getElementById('dhtSensorType').value = String(d.type);
    }
    if (d.success) {
        let h = '';
        if (d.type !== undefined) {
            h += '<div class="info-item"><div class="info-label" data-i18n="dht_sensor_type">' + tr('dht_sensor_type') + '</div><div class="info-value">' + (Number(d.type) === 22 ? tr('dht22_option') : tr('dht11_option')) + '</div></div>';
        }
        h += '<div class="info-item"><div class="info-label" data-i18n="temperature">' + tr('temperature') + '</div><div class="info-value">' + d.temperature.toFixed(1) + ' °C</div></div>';
        h += '<div class="info-item"><div class="info-label" data-i18n="humidity">' + tr('humidity') + '</div><div class="info-value">' + d.humidity.toFixed(1) + ' %</div></div>';
        document.getElementById('dht-results').innerHTML = h;
    }
    setStatus('dht-status', d.result, d.success ? 'success' : 'error');
}
async function applyLightConfig() {
    const pin = parseInt(document.getElementById('lightPin').value);
    const resp = await fetch('/api/light-sensor-config?pin=' + pin);
    const d = await resp.json();
    setStatus('light-status', d.message, d.success ? 'success' : 'error');
}
async function testLightSensor() {
    setStatus('light-status', {
        key: 'test_in_progress'
    }, null);
    document.getElementById('light-results').innerHTML = '';
    const r = await fetch('/api/light-sensor-test');
    const d = await r.json();
    if (d.success) {
        let h = '';
        h += '<div class="info-item"><div class="info-label" data-i18n="light_level">' + tr('light_level') + '</div><div class="info-value">' + d.value + ' / 4095</div></div>';
        document.getElementById('light-results').innerHTML = h;
    }
    setStatus('light-status', d.result, d.success ? 'success' : 'error');
}
async function applyDistanceConfig() {
    const trig = parseInt(document.getElementById('distTrig').value);
    const echo = parseInt(document.getElementById('distEcho').value);
    const resp = await fetch('/api/distance-sensor-config?trig=' + trig + '&echo=' + echo);
    const d = await resp.json();
    setStatus('distance-status', d.message, d.success ? 'success' : 'error');
}
async function testDistanceSensor() {
    setStatus('distance-status', {
        key: 'test_in_progress'
    }, null);
    document.getElementById('distance-results').innerHTML = '';
    const r = await fetch('/api/distance-sensor-test');
    const d = await r.json();
    if (d.success) {
        let h = '';
        h += '<div class="info-item"><div class="info-label" data-i18n="distance">' + tr('distance') + '</div><div class="info-value">' + d.distance.toFixed(2) + ' cm</div></div>';
        document.getElementById('distance-results').innerHTML = h;
    }
    setStatus('distance-status', d.result, d.success ? 'success' : 'error');
}
async function applyMotionConfig() {
    const pin = parseInt(document.getElementById('motionPin').value);
    const resp = await fetch('/api/motion-sensor-config?pin=' + pin);
    const d = await resp.json();
    setStatus('motion-status', d.message, d.success ? 'success' : 'error');
}
async function testMotionSensor() {
    setStatus('motion-status', {
        key: 'test_in_progress'
    }, null);
    document.getElementById('motion-results').innerHTML = '';
    const r = await fetch('/api/motion-sensor-test');
    const d = await r.json();
    if (d.success) {
        let h = '';
        const motionText = d.motion ? tr('motion_detected') : tr('no_motion');
        const motionBadge = d.motion ? 'badge-warning' : 'badge-success';
        h += '<div class="info-item"><div class="info-label">Status</div><div class="info-value"><span class="badge ' + motionBadge + '">' + motionText + '</span></div></div>';
        document.getElementById('motion-results').innerHTML = h;
    }
    setStatus('motion-status', d.result, d.success ? 'success' : 'error');
}
async function applySDConfig() {
    const miso = document.getElementById('sdMiso').value;
    const mosi = document.getElementById('sdMosi').value;
    const sclk = document.getElementById('sdSclk').value;
    const cs = document.getElementById('sdCs').value;
    setStatus('sd-status', {
        key: 'applying_config'
    }, null);
    const r = await fetch(`/api/sd-config?miso=${miso}&mosi=${mosi}&sclk=${sclk}&cs=${cs}`);
    const d = await r.json();
    setStatus('sd-status', d.message, d.success ? 'success' : 'error');
}
async function testSD() {
    setStatus('sd-status', {
        key: 'test_in_progress'
    }, null);
    const r = await fetch('/api/sd-test');
    const d = await r.json();
    setStatus('sd-status', d.result, d.success ? 'success' : 'error');
}
async function loadSDInfo() {
    setStatus('sd-status', {
        key: 'loading'
    }, null);
    const r = await fetch('/api/sd-info');
    const d = await r.json();
    const res = document.getElementById('sd-results');
    if (res && d.available) {
        res.innerHTML = '<div class="info-item"><div class="info-label">Type</div><div class="info-value">' + d.type + '</div></div>' + '<div class="info-item"><div class="info-label">Taille</div><div class="info-value">' + d.size_mb + ' MB</div></div>' + '<div class="info-item"><div class="info-label">Total</div><div class="info-value">' + d.total_mb + ' MB</div></div>' + '<div class="info-item"><div class="info-label">Utilisé</div><div class="info-value">' + d.used_mb + ' MB</div></div>';
    }
    setStatus('sd-status', d.result, d.available ? 'success' : 'error');
}
async function testSDRead() {
    setStatus('sd-status', {
        key: 'sd_read_test_running'
    }, null);
    const r = await fetch('/api/sd-test-read');
    const d = await r.json();
    setStatus('sd-status', d.result, d.success ? 'success' : 'error');
}
async function testSDWrite() {
    setStatus('sd-status', {
        key: 'sd_write_test_running'
    }, null);
    const r = await fetch('/api/sd-test-write');
    const d = await r.json();
    setStatus('sd-status', d.result, d.success ? 'success' : 'error');
}
async function formatSD() {
    if (!confirm(tr('sd_format_confirm'))) {
        return;
    }
    setStatus('sd-status', {
        key: 'sd_format_running'
    }, null);
    const r = await fetch('/api/sd-format');
    const d = await r.json();
    setStatus('sd-status', d.result, d.success ? 'success' : 'error');
}
async function applyRotaryConfig() {
    const clk = document.getElementById('rotaryClk').value;
    const dt = document.getElementById('rotaryDt').value;
    const sw = document.getElementById('rotarySw').value;
    setStatus('rotary-status', {
        key: 'applying_config'
    }, null);
    const r = await fetch(`/api/rotary-config?clk=${clk}&dt=${dt}&sw=${sw}`);
    const d = await r.json();
    setStatus('rotary-status', d.message, d.success ? 'success' : 'error');
}
async function testRotary() {
    setStatus('rotary-status', {
        key: 'test_in_progress'
    }, null);
    const r = await fetch('/api/rotary-test');
    const d = await r.json();
    setStatus('rotary-status', d.result, d.success ? 'success' : 'error');
}
let rotaryMonitorInterval = null;
async function toggleRotaryMonitoring() {
    const btn = document.getElementById('rotary-monitor-btn');
    if (rotaryMonitorInterval) {
        clearInterval(rotaryMonitorInterval);
        rotaryMonitorInterval = null;
        btn.textContent = '👁️ ' + tr('monitor_button');
        btn.className = 'btn btn-info';
        setStatus('rotary-status', tr('stop_monitoring'), null);
        return;
    }
    btn.textContent = '⏸️ ' + tr('stop_monitoring');
    btn.className = 'btn btn-danger';
    setStatus('rotary-status', tr('monitor_button') + '...', 'success');
    rotaryMonitorInterval = setInterval(async () => {
        const r = await fetch('/api/rotary-position');
        const d = await r.json();
        const posEl = document.getElementById('rotary-position');
        const btnEl = document.getElementById('rotary-button');
        if (posEl) posEl.textContent = d.position;
        if (btnEl) {
            btnEl.textContent = d.button_pressed ? tr('button_pressed') : tr('button_released');
            btnEl.style.color = d.button_pressed ? '#dc3545' : '#28a745';
            btnEl.style.fontWeight = d.button_pressed ? 'bold' : 'normal';
        }
    }, 100);
}
async function resetRotaryPosition() {
    const r = await fetch('/api/rotary-reset');
    const d = await r.json();
    setStatus('rotary-status', d.message, d.success ? 'success' : null);
    const posEl = document.getElementById('rotary-position');
    if (posEl) posEl.textContent = '0';
}
let bootButtonMonitorInterval = null;
async function toggleBootButtonMonitoring() {
    const btn = document.getElementById('boot-monitor-btn');
    if (bootButtonMonitorInterval) {
        clearInterval(bootButtonMonitorInterval);
        bootButtonMonitorInterval = null;
        btn.textContent = '👁️ ' + tr('monitor_button');
        btn.className = 'btn btn-info';
        return;
    }
    btn.textContent = '⏸️ ' + tr('stop_monitoring');
    btn.className = 'btn btn-danger';
    bootButtonMonitorInterval = setInterval(async () => {
        const r = await fetch('/api/button-state?button=boot');
        const d = await r.json();
        const stateEl = document.getElementById('boot-button-state');
        if (stateEl) {
            stateEl.textContent = d.pressed ? tr('button_pressed') : tr('button_released');
            stateEl.style.color = d.pressed ? '#dc3545' : '#28a745';
            stateEl.style.fontWeight = d.pressed ? 'bold' : 'normal';
        }
    }, 100);
}
let button1MonitorInterval = null;
async function toggleButton1Monitoring() {
    const btn = document.getElementById('button1-monitor-btn');
    if (button1MonitorInterval) {
        clearInterval(button1MonitorInterval);
        button1MonitorInterval = null;
        btn.textContent = '👁️ ' + tr('monitor_button');
        btn.className = 'btn btn-info';
        return;
    }
    btn.textContent = '⏸️ ' + tr('stop_monitoring');
    btn.className = 'btn btn-danger';
    button1MonitorInterval = setInterval(async () => {
        const r = await fetch('/api/button-state?button=1');
        const d = await r.json();
        const stateEl = document.getElementById('button1-state');
        if (stateEl) {
            stateEl.textContent = d.pressed ? tr('button_pressed') : tr('button_released');
            stateEl.style.color = d.pressed ? '#dc3545' : '#28a745';
            stateEl.style.fontWeight = d.pressed ? 'bold' : 'normal';
        }
    }, 100);
}
let button2MonitorInterval = null;
async function toggleButton2Monitoring() {
    const btn = document.getElementById('button2-monitor-btn');
    if (button2MonitorInterval) {
        clearInterval(button2MonitorInterval);
        button2MonitorInterval = null;
        btn.textContent = '👁️ ' + tr('monitor_button');
        btn.className = 'btn btn-info';
        return;
    }
    btn.textContent = '⏸️ ' + tr('stop_monitoring');
    btn.className = 'btn btn-danger';
    button2MonitorInterval = setInterval(async () => {
        const r = await fetch('/api/button-state?button=2');
        const d = await r.json();
        const stateEl = document.getElementById('button2-state');
        if (stateEl) {
            stateEl.textContent = d.pressed ? tr('button_pressed') : tr('button_released');
            stateEl.style.color = d.pressed ? '#dc3545' : '#28a745';
            stateEl.style.fontWeight = d.pressed ? 'bold' : 'normal';
        }
    }, 100);
}
async function applyButtonConfig(buttonId) {
    const pin = document.getElementById(buttonId + '-pin').value;
    const r = await fetch(`/api/button-config?button=${buttonId}&pin=${pin}`);
    const d = await r.json();
    alert(d.message);
}

function formatUptime(ms) {
    const s = Math.floor(ms / 1000),
        m = Math.floor(s / 60),
        h = Math.floor(m / 60),
        d = Math.floor(h / 24);
    return d + 'j ' + (h % 24) + 'h ' + (m % 60) + 'm';
}

function showUpdateIndicator() {
    document.getElementById('updateIndicator').classList.add('show');
}

function hideUpdateIndicator() {
    setTimeout(() => document.getElementById('updateIndicator').classList.remove('show'), 500);
}

function updateStatusIndicator(c) {
    const i = document.getElementById('statusIndicator');
    if (c) {
        i.classList.remove('status-offline');
        i.classList.add('status-online');
    } else {
        i.classList.remove('status-online');
        i.classList.add('status-offline');
    }
}

function setStatus(id, value, state) {
    const el = document.getElementById(id);
    if (!el) {
        return;
    }
    el.classList.remove('success', 'error');
    if (state) {
        el.classList.add(state);
    } else {
        el.classList.remove('success', 'error');
    }
    if (value && typeof value === 'object' && typeof value.key === 'string') {
        setElementTranslation(el, value);
    } else if (value && typeof value === 'object' && typeof value.text === 'string') {
        clearTranslationAttributes(el);
        el.textContent = value.text;
    } else if (typeof value === 'string') {
        clearTranslationAttributes(el);
        el.textContent = value;
    } else if (value == null) {
        translateElement(el, getCurrentTranslations());
    }
}

function updateRealtimeValues(d) {
    const u = document.getElementById('uptime');
    if (u) u.textContent = formatUptime(d.uptime);
    const t = document.getElementById('temperature');
    if (t && d.temperature !== -999) t.textContent = d.temperature.toFixed(1) + ' °C';
    const sf = document.getElementById('sram-free');
    if (sf) sf.textContent = (d.sram.free / 1024).toFixed(2) + ' KB';
    const su = document.getElementById('sram-used');
    if (su) su.textContent = (d.sram.used / 1024).toFixed(2) + ' KB';
    const sp = document.getElementById('sram-progress');
    if (sp && d.sram.total > 0) {
        const pct = ((d.sram.used / d.sram.total) * 100).toFixed(1);
        sp.style.width = pct + '%';
        sp.textContent = pct + '%';
    }
    if (d.psram && d.psram.total > 0) {
        const pf = document.getElementById('psram-free');
        if (pf) pf.textContent = (d.psram.free / 1048576).toFixed(2) + ' MB';
        const pu = document.getElementById('psram-used');
        if (pu) pu.textContent = (d.psram.used / 1048576).toFixed(2) + ' MB';
        const pp = document.getElementById('psram-progress');
        if (pp) {
            const pct = ((d.psram.used / d.psram.total) * 100).toFixed(1);
            pp.style.width = pct + '%';
            pp.textContent = pct + '%';
        }
    }
    const f = document.getElementById('fragmentation');
    if (f) f.textContent = d.fragmentation.toFixed(1) + '%';
}

function changeLang(lang, btn) {
    fetch('/api/set-language?lang=' + lang, {
        cache: 'no-store'
    }).then(r => r.json()).then(d => {
        if (!d.success) {
            throw new Error('language switch rejected');
        }
        currentLang = lang;
        document.documentElement.setAttribute('lang', lang);
        document.querySelectorAll('.lang-btn').forEach(b => b.classList.remove('active'));
        if (btn) {
            btn.classList.add('active');
        } else {
            document.querySelectorAll('.lang-btn').forEach(b => {
                if (b.getAttribute('data-lang') === lang) {
                    b.classList.add('active');
                }
            });
        }
        return fetchTranslations(lang);
    }).then(translations => {
        setTranslationsCache(translations);
        updateInterfaceTexts();
        const ind = document.getElementById('updateIndicator');
        if (ind) {
            setElementTranslation(ind, {
                key: 'language_updated',
                suffix: ' (' + lang.toUpperCase() + ')'
            });
            showUpdateIndicator();
            hideUpdateIndicator();
        }
    }).catch(err => {
        const ind = document.getElementById('updateIndicator');
        if (ind) {
            setElementTranslation(ind, {
                key: 'language_switch_error',
                suffix: ': ' + String(err)
            });
            showUpdateIndicator();
            hideUpdateIndicator();
        }
        refetchTranslations().catch(retryErr => console.error('Translations retry failed', retryErr));
    });
}
