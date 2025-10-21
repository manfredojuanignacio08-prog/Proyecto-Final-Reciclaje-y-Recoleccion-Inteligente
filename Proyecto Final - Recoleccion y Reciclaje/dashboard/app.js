// --- Helpers ---
const $ = s => document.querySelector(s);
const $$ = s => Array.from(document.querySelectorAll(s));

// --- Global Settings & State ---
const settings = Object.assign({
    apiBase: 'http://127.0.0.1:5000', // Reemplaza con la IP de tu API si es necesario
    alertThresh: 80,
    binsData: [],
    eventsData: [],
}, JSON.parse(localStorage.getItem('eco:settings') || '{}'));

// Objeto para los iconos de los tachos
const binIcons = {
    "Metal": "🔩",
    "Orgánico": "🍎",
    "Resto": "📦"
};

function saveSettings() {
    localStorage.setItem('eco:settings', JSON.stringify(settings));
}

function api(path) {
    return `${settings.apiBase}${path}`;
}

// --- API Communication ---
function setOnline(isOnline) {
    const indicator = $('#status-indicator');
    indicator.classList.toggle('connected', isOnline);
    indicator.classList.toggle('disconnected', !isOnline);
    indicator.title = isOnline ? 'API Conectada' : 'API Desconectada';
}

async function fetchBins() {
    try {
        const r = await fetch(api('/api/bins'), { cache: 'no-store' });
        if (!r.ok) throw new Error("API failed");
        settings.binsData = await r.json();
        setOnline(true);
    } catch {
        settings.binsData = [
            { id: 1, name: "Metal", grams: 0, percent: 0, capacity_kg: 5.0 },
            { id: 2, name: "Orgánico", grams: 0, percent: 0, capacity_kg: 5.0 },
            { id: 3, name: "Resto", grams: 0, percent: 0, capacity_kg: 5.0 },
        ];
        setOnline(false);
    }
}

async function fetchEvents() {
    try {
        const r = await fetch(api('/api/deposits/recent?limit=20'), { cache: 'no-store' });
        if (!r.ok) throw new Error("API failed");
        settings.eventsData = await r.json();
        setOnline(true);
    } catch {
        settings.eventsData = [];
        setOnline(false);
    }
}

async function updateAxisState() {
    const stateEl = $('#axisState');
    try {
        const r = await fetch(api('/api/axis/state'), { cache: 'no-store' });
        if (!r.ok) throw new Error("API failed");

        const j = await r.json();
        const d = j.data || j;

        stateEl.textContent = d.state || 'DESCONOCIDO';
        stateEl.className = `axis-value status-text status-${(d.state || 'unknown').toLowerCase()}`;

        $('#axisHome').textContent = d.homed ? 'Sí' : 'No';
        $('#axisPos').textContent = `${d.pos_mm ?? 0} mm`;
        setOnline(true);
    } catch {
        stateEl.textContent = 'ERROR';
        stateEl.className = 'axis-value status-text status-error';
        $('#axisHome').textContent = '--';
        $('#axisPos').textContent = '-- mm';
        setOnline(false);
    }
}

// --- Rendering Functions ---

// VVV FUNCIÓN MODIFICADA VVV
function renderBins(selector) {
    const container = $(selector);
    container.innerHTML = '';
    const bins = settings.binsData;

    for (const bin of bins) {
        const p = bin.percent;
        let fillClass = 'bin-fill';
        if (p >= settings.alertThresh) fillClass += ' danger';
        else if (p >= 60) fillClass += ' warning';
        
        const icon = binIcons[bin.name] || '♻️';

        const card = document.createElement('div');
        card.className = 'bin-card-graphic';
        card.innerHTML = `
            <div class="bin-title-graphic">${bin.id}: ${bin.name}</div>
            <div class="bin-graphic-container">
                <div class="bin-lid"><div class="bin-lid-handle"></div></div>
                <div class="bin-body">
                    <div class="${fillClass}" style="height: ${p}%"></div>
                    <div class="bin-icon-watermark">${icon}</div>
                    <div class="bin-percentage">${p}%</div>
                </div>
            </div>
            <div class="bin-details">
                <span class="bin-weight-graphic">${bin.grams}g</span>
                <span>Cap: ${bin.capacity_kg} kg</span>
            </div>
        `;
        container.appendChild(card);
    }
}
// ^^^ FUNCIÓN MODIFICADA ^^^

function renderKPIs() {
    const totalWeight = settings.binsData.reduce((sum, bin) => sum + bin.grams, 0);
    const totalItems = settings.eventsData.length;

    let mostFrequent = 'Ninguno';
    if (totalItems > 0) {
        const counts = settings.eventsData.reduce((acc, evt) => {
            acc[evt.material] = (acc[evt.material] || 0) + 1;
            return acc;
        }, {});
        mostFrequent = Object.keys(counts).reduce((a, b) => counts[a] > counts[b] ? a : b);
    }

    $('#totalWeight').textContent = totalWeight;
    $('#totalItems').textContent = totalItems;
    $('#mostFrequent').textContent = mostFrequent;
}

function renderEvents() {
    const tbody = $('#events-table-body');
    tbody.innerHTML = '';
    const events = settings.eventsData;
    if (events.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;">No hay eventos recientes.</td></tr>';
        return;
    }

    for (const evt of events) {
        const row = tbody.insertRow(0);
        const materialClass = (evt.material || 'desconocido').toLowerCase().replace(/[\s\/]/g, '');
        row.innerHTML = `
      <td>${evt.ts}</td>
      <td>Tacho ${evt.bin}</td>
      <td><span class="tag tag-${materialClass}">${evt.material}</span></td>
      <td>${evt.delta_g}</td>
      <td>${evt.fill_percent}</td>
    `;
    }
}

async function fetchAndRenderConfig() {
    try {
        const r = await fetch(api('/api/config'), { cache: 'no-store' });
        if (!r.ok) return;
        const cfg = await r.json();

        if (cfg.steps_per_mm) $('#stepsPerMm').value = cfg.steps_per_mm;
        if (cfg.v_max_mm_s) $('#vmax').value = cfg.v_max_mm_s;
        if (cfg.a_max_mm_s2) $('#acc').value = cfg.a_max_mm_s2;
        if (cfg.bin_positions_mm && cfg.bin_positions_mm.length === 3) {
            $('#p1').value = cfg.bin_positions_mm[0];
            $('#p2').value = cfg.bin_positions_mm[1];
            $('#p3').value = cfg.bin_positions_mm[2];
        }
    } catch (e) {
        console.error("No se pudo cargar la configuración de la API", e);
    }
}

// --- Main Refresh Loop ---
async function refreshAll() {
    await Promise.all([
        fetchBins(),
        fetchEvents(),
        updateAxisState()
    ]);
    renderBins('#binCards');
    renderKPIs();
    renderEvents();
}

// --- Event Listeners ---
document.addEventListener('DOMContentLoaded', () => {
    // Theme setup
    const themeToggle = $('#theme-toggle');
    const storedTheme = localStorage.getItem('theme') || 'light';
    document.body.className = storedTheme === 'dark' ? 'dark-mode' : '';
    themeToggle.textContent = storedTheme === 'dark' ? '☀️' : '🌙';

    themeToggle.addEventListener('click', () => {
        document.body.classList.toggle('dark-mode');
        const isDark = document.body.classList.contains('dark-mode');
        themeToggle.textContent = isDark ? '☀️' : '🌙';
        localStorage.setItem('theme', isDark ? 'dark' : 'light');
    });

    // Save axis config button
    $('#saveAxis').addEventListener('click', async () => {
        const body = {
            steps_per_mm: Number($('#stepsPerMm').value),
            v_max_mm_s: Number($('#vmax').value),
            a_max_mm_s2: Number($('#acc').value),
            bin_positions_mm: [
                Number($('#p1').value), Number($('#p2').value),
                Number($('#p3').value)
            ]
        };
        try {
            const r = await fetch(api('/api/config'), {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(body)
            });
            alert(r.ok ? 'Configuración guardada' : 'Error al guardar');
        } catch {
            alert('API no disponible. No se pudo guardar.');
        }
    });
    
    // Refresh axis state button
    $('#axisRefresh').addEventListener('click', updateAxisState);

    // Initial load and periodic refresh
    refreshAll();
    fetchAndRenderConfig();
    setInterval(refreshAll, 5000);
});

// Service Worker Registration
if ('serviceWorker' in navigator) {
    window.addEventListener('load', () => {
        navigator.serviceWorker.register('./sw.js')
            .then(reg => console.log('Service Worker registrado', reg))
            .catch(err => console.log('Error registrando Service Worker', err));
    });
}