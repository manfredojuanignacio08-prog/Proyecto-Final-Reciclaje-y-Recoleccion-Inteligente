/**
 * EcoSmart Dashboard - app.js (Corregido y Reorganizado)
 */

// --- Helpers ---
const $ = s => document.querySelector(s);
const $$ = s => Array.from(document.querySelectorAll(s));

// --- Global Settings & State ---
const settings = Object.assign({
    apiBase: 'http://127.0.0.1:5000', // Reemplaza con la IP de tu API si es necesario
    alertThresh: 80,
    binsData: [],
    eventsData: [],
    historyData: { labels: [], bins: { "1": [], "2": [], "3": [] }, total: [] }, // Estado para el historial
}, JSON.parse(localStorage.getItem('eco:settings') || '{}'));

// Objeto para los iconos de los tachos
const binIcons = {
    "Metal": "🔩",
    "Orgánico": "🍎",
    "Resto": "📦"
};

let historyChart = null; // Variable global para la instancia del gráfico
let isRefreshing = false; // Flag para evitar refrescos concurrentes
let socket = null; // Variable global para la instancia de Socket.IO

function saveSettings() {
    localStorage.setItem('eco:settings', JSON.stringify(settings));
}

function api(path) {
    // Asegura que el path comience con /
    const formattedPath = path.startsWith('/') ? path : `/${path}`;
    return `${settings.apiBase}${formattedPath}`;
}

// --- API Communication ---
function setOnline(isOnline) {
    const indicator = $('#status-indicator');
    if (!indicator) return;
    indicator.classList.toggle('connected', isOnline);
    indicator.classList.toggle('disconnected', !isOnline);
    indicator.title = isOnline ? 'API Conectada' : 'API Desconectada';
}

// REEMPLAZAR las funciones apiJog y apiHome en tu app.js:

/** Envia un comando de jog a la API */
async function apiJog(deltaMm) {
    console.log(`Enviando comando jog: ${deltaMm} mm`);
    try {
        const r = await fetch(api('/api/axis/jog'), {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ mm: deltaMm })
        });
        
        if (r.ok) {
            const result = await r.json();
            console.log('Jog exitoso:', result);
            setOnline(true);
        } else {
            console.error('Comando jog falló:', r.status);
            // Si falla el endpoint nuevo, intentar con el sistema de colas
            await sendCommandToQueue({ jog_mm: deltaMm });
        }
    } catch (e) {
        console.error('Error enviando comando jog:', e);
        setOnline(false);
    }
}

/** Envia un comando de home a la API */
async function apiHome() {
    console.log('Enviando comando home');
    try {
        const r = await fetch(api('/api/axis/home'), { 
            method: 'POST',
            headers: {'Content-Type': 'application/json'}
        });
        
        if (r.ok) {
            const result = await r.json();
            console.log('Home exitoso:', result);
            setOnline(true);
        } else {
            console.error('Comando home falló:', r.status);
            // Si falla el endpoint nuevo, intentar con el sistema de colas
            await sendCommandToQueue({ home: true });
        }
    } catch (e) {
        console.error('Error enviando comando home:', e);
        setOnline(false);
    }
}

/** Función de respaldo usando sistema de colas */
async function sendCommandToQueue(command) {
    try {
        const r = await fetch(api('/api/axis/command'), {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify(command)
        });
        
        if (r.ok) {
            console.log('Comando enviado a cola:', command);
        } else {
            console.error('Error enviando comando a cola:', r.status);
        }
    } catch (e) {
        console.error('Error de red enviando comando a cola:', e);
    }
}
async function fetchBins() {
    try {
        const r = await fetch(api('/api/bins'), { cache: 'no-store' });
        if (!r.ok) throw new Error(`API failed with status ${r.status}`);
        // Maneja respuesta plana o {ok, data}
        const jsonData = await r.json();
        settings.binsData = Array.isArray(jsonData.data) ? jsonData.data : Array.isArray(jsonData) ? jsonData : [];
        setOnline(true);
    } catch (e) {
        console.error("Error fetching bins:", e);
        // Fallback data
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
        if (!r.ok) throw new Error(`API failed with status ${r.status}`);
        // Maneja respuesta plana o {ok, data}
        const jsonData = await r.json();
        settings.eventsData = Array.isArray(jsonData.data) ? jsonData.data : Array.isArray(jsonData) ? jsonData : [];
        setOnline(true);
    } catch(e) {
        console.error("Error fetching events:", e);
        settings.eventsData = []; // Reinicia si falla
        setOnline(false);
    }
}

async function fetchHistory(days = 7) {
  try {
    const r = await fetch(api(`/api/deposits/history?days=${days}`), { cache: 'no-store' });
    if (!r.ok) throw new Error(`API failed with status ${r.status}`);
    const j = await r.json();
    settings.historyData = j.data || j; // Actualiza el estado global
  } catch (e) {
    console.error("Error fetching history:", e);
    // Fallback vacío
    settings.historyData = { labels: [], bins: { "1": [], "2": [], "3": [] }, total: [] };
  }
}

async function updateAxisState() {
    const stateEl = $('#axisState');
    if (!stateEl) return;
    try {
        const r = await fetch(api('/api/axis/state'), { cache: 'no-store' });
        if (!r.ok) throw new Error(`API failed with status ${r.status}`);
        const j = await r.json();
        const d = j.data || j; // Handle both {ok, data} and direct object

        updateAxisUI(d); // Llama a la función de actualización de UI
        setOnline(true);
    } catch (e) {
        console.error("Error updating axis state via fetch:", e);
        // Muestra estado de error en UI
        updateAxisUI({ state: 'ERROR', homed: false, pos_mm: null });
        setOnline(false);
    }
}

// --- Rendering Functions ---

// Función separada para actualizar la UI del eje (usada por fetch y socket)
function updateAxisUI(stateData) {
    const stateEl = $('#axisState');
    if (stateEl) {
        stateEl.textContent = stateData.state || 'DESCONOCIDO';
        stateEl.className = `axis-value status-text status-${(stateData.state || 'unknown').toLowerCase()}`;
    }
    const homeEl = $('#axisHome');
    if (homeEl) homeEl.textContent = stateData.homed ? 'Sí' : 'No';
    const posEl = $('#axisPos');
    if (posEl) {
        const posMm = (typeof stateData.pos_mm === 'number' && !isNaN(stateData.pos_mm)) ? stateData.pos_mm.toFixed(1) : '--';
        posEl.textContent = `${posMm} mm`;
    }
}

function renderBins(selector = '#binCards') {
    const container = $(selector);
    if (!container) return;
    container.innerHTML = '';
    const bins = Array.isArray(settings.binsData) ? settings.binsData : [];

    if (bins.length === 0) {
        // Opcional: Mostrar mensaje si no hay datos de tachos
        container.innerHTML = '<p style="grid-column: 1 / -1; text-align: center; color: var(--text-secondary);">No se pudieron cargar los datos de los tachos.</p>';
        return;
    }

    bins.forEach(bin => {
        const p = (typeof bin.percent === 'number' && !isNaN(bin.percent)) ? Math.max(0, Math.min(100, bin.percent)) : 0;
        let fillClass = 'bin-fill';
        if (p >= settings.alertThresh) fillClass += ' danger';
        else if (p >= 60) fillClass += ' warning';

        const icon = binIcons[bin.name] || '♻️';
        const grams = (typeof bin.grams === 'number' && !isNaN(bin.grams)) ? bin.grams : 0;
        const capacity = (typeof bin.capacity_kg === 'number' && !isNaN(bin.capacity_kg)) ? bin.capacity_kg : 5.0;

        const card = document.createElement('div');
        card.className = 'bin-card-graphic';
        card.innerHTML = `
            <div class="bin-title-graphic">${bin.id || '?'}: ${bin.name || 'Desconocido'}</div>
            <div class="bin-graphic-container">
                <div class="bin-lid"><div class="bin-lid-handle"></div></div>
                <div class="bin-body">
                    <div class="${fillClass}" style="height: ${p}%"></div>
                    <div class="bin-icon-watermark">${icon}</div>
                    <div class="bin-percentage">${p}%</div>
                </div>
            </div>
            <div class="bin-details">
                <span class="bin-weight-graphic">${grams}g</span>
                <span>Cap: ${capacity} kg</span>
            </div>
        `;
        container.appendChild(card);
    });
}

function renderKPIs() {
    const bins = Array.isArray(settings.binsData) ? settings.binsData : [];
    const totalWeight = bins.reduce((sum, bin) => sum + ((typeof bin.grams === 'number' && !isNaN(bin.grams)) ? bin.grams : 0), 0);
    const events = Array.isArray(settings.eventsData) ? settings.eventsData : [];
    const totalItems = events.length;

    let mostFrequent = 'Ninguno';
    if (totalItems > 0) {
        const counts = events.reduce((acc, evt) => {
            const material = evt.material || 'Desconocido';
            acc[material] = (acc[material] || 0) + 1;
            return acc;
        }, {});
        if (Object.keys(counts).length > 0) {
           mostFrequent = Object.keys(counts).reduce((a, b) => counts[a] > counts[b] ? a : b);
        }
    }

    const totalWeightEl = $('#totalWeight');
    if (totalWeightEl) totalWeightEl.textContent = totalWeight;
    const totalItemsEl = $('#totalItems');
    if (totalItemsEl) totalItemsEl.textContent = totalItems;
    const mostFrequentEl = $('#mostFrequent');
    if (mostFrequentEl) mostFrequentEl.textContent = mostFrequent;
}

function renderEvents(animate = false) {
    const tbody = $('#events-table-body');
    if (!tbody) return;
    tbody.innerHTML = ''; // Limpia tabla antes de re-renderizar
    const events = Array.isArray(settings.eventsData) ? settings.eventsData : [];

    if (events.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; padding: 1rem; color: var(--text-secondary);">No hay eventos recientes.</td></tr>';
        return;
    }

    // Ordena por timestamp descendente (más nuevo primero) SIEMPRE antes de mostrar
    const sortedEvents = [...events].sort((a, b) => (b.ts || '').localeCompare(a.ts || ''));

    sortedEvents.forEach((evt, index) => {
        const row = tbody.insertRow(); // Inserta al final

        const timestamp = evt.ts ? new Date(evt.ts).toLocaleString('es-AR', { dateStyle: 'short', timeStyle: 'medium'}) : 'Fecha desconocida';
        const binId = evt.bin || '?';
        const material = evt.material || 'Desconocido';
        const materialClass = material.toLowerCase().replace(/[\s\/]/g, '');
        const deltaG = (typeof evt.delta_g === 'number' && !isNaN(evt.delta_g)) ? evt.delta_g : '?';
        const fillPercent = (typeof evt.fill_percent === 'number' && !isNaN(evt.fill_percent)) ? evt.fill_percent : '?';

        row.innerHTML = `
            <td>${timestamp}</td>
            <td>Tacho ${binId}</td>
            <td><span class="tag tag-${materialClass}">${material}</span></td>
            <td>${deltaG}</td>
            <td>${fillPercent}%</td>
        `;
        // Aplica animación solo a la primera fila si viene de un evento de socket
        if (animate && index === 0) {
             row.style.animation = 'slideIn 0.5s ease-out forwards';
        }
    });
}

function renderHistoryChart(histData) {
    const ctx = document.getElementById('historyChart');
    if (!ctx) return;

    // Colores base para los gráficos
    const colors = {
        metal: 'rgba(120, 144, 156, 0.7)', // Gris azulado
        organico: 'rgba(141, 110, 99, 0.7)', // Marrón
        resto: 'rgba(97, 97, 97, 0.7)',     // Gris oscuro
        total: 'rgba(97, 180, 107, 1)',      // Verde principal
        borderWidth: 2,
        totalBorderWidth: 3
    };
     // Colores para modo oscuro
    const darkColors = {
        metal: 'rgba(176, 190, 197, 0.7)',
        organico: 'rgba(188, 170, 164, 0.7)',
        resto: 'rgba(189, 189, 189, 0.7)',
        total: 'rgba(97, 180, 107, 1)',
        borderWidth: 2,
        totalBorderWidth: 3
    };

    const currentColors = document.body.classList.contains('dark-mode') ? darkColors : colors;
    const gridColor = document.body.classList.contains('dark-mode') ? 'rgba(148, 163, 184, 0.2)' : 'rgba(0, 0, 0, 0.1)';
    const textColor = document.body.classList.contains('dark-mode') ? '#e2e8f0' : '#1a202c';

    const data = {
        labels: histData.labels || [],
        datasets: [
            { label: 'Metal', data: histData.bins["1"] || [], borderColor: currentColors.metal, tension: 0.1, borderWidth: currentColors.borderWidth },
            { label: 'Orgánico', data: histData.bins["2"] || [], borderColor: currentColors.organico, tension: 0.1, borderWidth: currentColors.borderWidth },
            { label: 'Resto', data: histData.bins["3"] || [], borderColor: currentColors.resto, tension: 0.1, borderWidth: currentColors.borderWidth },
            { label: 'Total', data: histData.total || [], borderColor: currentColors.total, tension: 0.1, borderWidth: currentColors.totalBorderWidth }
        ]
    };

    const options = {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
            y: {
                beginAtZero: true,
                title: { display: true, text: 'Gramos', color: textColor },
                grid: { color: gridColor },
                ticks: { color: textColor }
            },
            x: {
                title: { display: true, text: 'Fecha', color: textColor },
                grid: { color: gridColor },
                ticks: { color: textColor }
            }
        },
        plugins: {
            legend: { labels: { color: textColor } }
        }
    };

    if (historyChart) {
        historyChart.data = data; // Actualiza datos
        historyChart.options = options; // Actualiza opciones (para cambio de tema)
        historyChart.update(); // Re-renderiza
    } else {
        historyChart = new Chart(ctx, { type: 'line', data, options });
    }
}

// SOLO UNA definición de fetchAndRenderConfig
async function fetchAndRenderConfig() {
    try {
        const r = await fetch(api('/api/config'), { cache: 'no-store' });
        if (!r.ok) throw new Error(`API failed with status ${r.status}`);
        const cfgData = await r.json();
        const cfg = cfgData.data || cfgData; // Maneja {ok, data} o plano

        // Actualiza campos del formulario si existen y los datos son válidos
        const stepsEl = $('#stepsPerMm');
        if (stepsEl && typeof cfg.steps_per_mm === 'number' && !isNaN(cfg.steps_per_mm)) stepsEl.value = cfg.steps_per_mm;
        const vmaxEl = $('#vmax');
        if (vmaxEl && typeof cfg.v_max_mm_s === 'number' && !isNaN(cfg.v_max_mm_s)) vmaxEl.value = cfg.v_max_mm_s;
        const accEl = $('#acc');
        if (accEl && typeof cfg.a_max_mm_s2 === 'number' && !isNaN(cfg.a_max_mm_s2)) accEl.value = cfg.a_max_mm_s2;

        if (Array.isArray(cfg.bin_positions_mm) && cfg.bin_positions_mm.length === 3) {
            const p1El = $('#p1');
            if (p1El && typeof cfg.bin_positions_mm[0] === 'number' && !isNaN(cfg.bin_positions_mm[0])) p1El.value = cfg.bin_positions_mm[0];
            const p2El = $('#p2');
            if (p2El && typeof cfg.bin_positions_mm[1] === 'number' && !isNaN(cfg.bin_positions_mm[1])) p2El.value = cfg.bin_positions_mm[1];
            const p3El = $('#p3');
            if (p3El && typeof cfg.bin_positions_mm[2] === 'number' && !isNaN(cfg.bin_positions_mm[2])) p3El.value = cfg.bin_positions_mm[2];
        }
        // Actualiza umbral de alerta global
        if (typeof cfg.threshold_percent === 'number' && !isNaN(cfg.threshold_percent)) {
            settings.alertThresh = cfg.threshold_percent;
        }
    } catch (e) {
        console.error("No se pudo cargar o parsear la configuración de la API", e);
        // Opcional: Mostrar un mensaje al usuario
        // alert("Error al cargar la configuración del eje desde la API.");
    }
}

// --- Main Refresh Loop ---
async function refreshAll() {
    if (isRefreshing) return;
    isRefreshing = true;
    console.log("Refreshing data...");
    try {
        await Promise.all([
            fetchBins(),
            fetchEvents(),
            updateAxisState(),
            fetchHistory(7) // Carga historial también
        ]);
        renderBins();
        renderKPIs();
        renderEvents();
        renderHistoryChart(settings.historyData); // Renderiza gráfico con datos actualizados
    } catch (error) {
        console.error("Error during refreshAll:", error);
        setOnline(false);
    } finally {
        isRefreshing = false;
    }
}

// --- Setup Realtime (Socket.IO) ---
function setupRealtime() {
    // Evita múltiples conexiones si ya existe
    if (socket && socket.connected) {
        return socket;
    }
    try {
        // Asegúrate de que la URL base sea correcta
        console.log(`Connecting Socket.IO to: ${settings.apiBase}`);
        socket = io(settings.apiBase, {
            transports: ['websocket'], // Prioriza WebSocket
            reconnectionAttempts: 5,   // Limita reintentos
            timeout: 10000             // Timeout de conexión
        });

        socket.on('connect', () => {
            console.log('Socket.IO conectado');
            setOnline(true);
        });

        socket.on('disconnect', (reason) => {
            console.log('Socket.IO desconectado:', reason);
            setOnline(false);
            // Intenta reconectar manualmente si es necesario (opcional)
            // if (reason === 'io server disconnect') { socket.connect(); }
        });

        socket.on('connect_error', (err) => {
             console.error('Socket.IO Connection Error:', err.message);
             setOnline(false);
        });

        // Listener 'axis': Actualiza UI directamente
        socket.on('axis', (stateData) => {
            console.log('Axis state received via Socket.IO:', stateData);
            updateAxisUI(stateData);
        });

        // Listener 'deposit': Actualización optimizada
        socket.on('deposit', (depositData) => {
            console.log('Deposit received via Socket.IO:', depositData);
            // 1. Añade a datos locales
            if (!Array.isArray(settings.eventsData)) settings.eventsData = [];
            settings.eventsData.unshift(depositData); // Añade al principio
            // Limita tamaño (opcional)
            if (settings.eventsData.length > 50) settings.eventsData.pop();

            // 2. Refresca bins (para pesos actualizados) y renderiza Bins/KPIs
            fetchBins().then(() => {
                renderBins();
                renderKPIs();
            });
            // 3. Renderiza tabla de eventos con animación para la nueva fila
            renderEvents(true);
            // 4. Opcional: Refresca historial si es relevante para el gráfico
            // fetchHistory(7).then(() => renderHistoryChart(settings.historyData));
        });

        return socket; // Devuelve la instancia

    } catch (e) {
        console.error("Error initializing Socket.IO:", e);
        setOnline(false);
        return null; // Devuelve null si falla la inicialización
    }
}


// --- Event Listeners and Initialization ---
document.addEventListener('DOMContentLoaded', () => {
    console.log("DOM Cargado. Inicializando...");

    // --- 1. Setup Theme ---
    const themeToggle = $('#theme-toggle');
    const body = document.body;
    const applyTheme = (theme) => { /* ... (definición de applyTheme como antes) ... */
        if (theme === 'dark-mode') {
            body.classList.add('dark-mode');
            if (themeToggle) themeToggle.textContent = '🌙';
            localStorage.setItem('theme', 'dark-mode');
        } else {
            body.classList.remove('dark-mode');
            if (themeToggle) themeToggle.textContent = '☀️';
            localStorage.setItem('theme', 'light-mode');
        }
    };
    const storedTheme = localStorage.getItem('theme');
    const systemPrefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    applyTheme(storedTheme || (systemPrefersDark ? 'dark-mode' : 'light-mode'));
    if (themeToggle) {
        themeToggle.addEventListener('click', () => {
            const isDarkMode = body.classList.contains('dark-mode');
            applyTheme(isDarkMode ? 'light-mode' : 'dark-mode');
            // Re-render chart if theme changes
            if (historyChart) {
                renderHistoryChart(settings.historyData);
            }
        });
    }

    // --- 2. Setup Event Listeners for Controls ---
    const jogNegBtn = $('#jogNeg');
    const jogPosBtn = $('#jogPos');
    const homeBtn = $('#homeBtn');
    const jogStepInput = $('#jogStep');
    const saveAxisBtn = $('#saveAxis');
    const axisRefreshBtn = $('#axisRefresh');

    // Jog Buttons
    if (jogNegBtn && jogStepInput) {
        jogNegBtn.addEventListener('click', () => {
            const step = parseFloat(jogStepInput.value) || 5; // Usa 5 como default
            apiJog(-Math.abs(step));
        });
    }
    if (jogPosBtn && jogStepInput) {
        jogPosBtn.addEventListener('click', () => {
            const step = parseFloat(jogStepInput.value) || 5;
            apiJog(Math.abs(step));
        });
    }
    // Home Button
    if (homeBtn) {
        homeBtn.addEventListener('click', apiHome);
    }
    // Refresh Axis Button
    if (axisRefreshBtn) {
        axisRefreshBtn.addEventListener('click', async () => {
            axisRefreshBtn.disabled = true;
            axisRefreshBtn.style.opacity = '0.6';
            await updateAxisState();
            setTimeout(() => {
                axisRefreshBtn.disabled = false;
                axisRefreshBtn.style.opacity = '1';
            }, 500);
        });
    }
    // Save Config Button
    if (saveAxisBtn) {
        saveAxisBtn.addEventListener('click', async () => {
            const button = saveAxisBtn;
            const originalHTML = button.innerHTML;
            // Basic validation
            const p1 = Number($('#p1')?.value); // Usa optional chaining
            const p2 = Number($('#p2')?.value);
            const p3 = Number($('#p3')?.value);
            const steps = Number($('#stepsPerMm')?.value);
            const vmax = Number($('#vmax')?.value);
            const acc = Number($('#acc')?.value);

            if (isNaN(p1) || isNaN(p2) || isNaN(p3) || isNaN(steps) || isNaN(vmax) || isNaN(acc) ||
                p1 < 0 || p2 <= p1 || p3 <= p2 || steps <= 0 || vmax <= 0 || acc <= 0) {
                alert('Valores de configuración inválidos. Asegúrate de que todos los números sean positivos y las posiciones sean crecientes (P1 ≤ P2 ≤ P3).');
                return;
            }

            button.disabled = true;
            button.innerHTML = '<span class="loader"></span> Guardando...';

            const body = {
                steps_per_mm: steps, v_max_mm_s: vmax, a_max_mm_s2: acc,
                bin_positions_mm: [p1, p2, p3]
            };
            try {
                const r = await fetch(api('/api/config'), {
                    method: 'POST', headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(body)
                });
                setOnline(r.ok);
                if (r.ok) {
                    button.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" style="vertical-align: middle; margin-right: 5px;"><path d="M20 6 9 17l-5-5"></path></svg> Guardado';
                    // Re-fetch config para asegurar que los inputs reflejen lo guardado
                    await fetchAndRenderConfig();
                } else {
                    throw new Error(`API returned ${r.status}: ${await r.text()}`);
                }
            } catch (e) {
                console.error("Error saving config:", e);
                setOnline(false);
                button.innerHTML = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" style="vertical-align: middle; margin-right: 5px;"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg> Error';
                alert(`Error al guardar: ${e.message}`);
            } finally {
                setTimeout(() => {
                    button.innerHTML = originalHTML;
                    button.disabled = false;
                }, 2500); // Muestra estado por 2.5 seg
            }
        });
    }

    // --- 3. Keyboard Shortcuts ---
    window.addEventListener('keydown', (e) => {
        if (document.activeElement && ['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName)) {
            return; // Ignora si el foco está en un input
        }
        // Usa el valor del input #jogStep si existe, sino usa 1mm/10mm
        const inputStep = parseFloat(jogStepInput?.value);
        const baseStep = (!isNaN(inputStep) && inputStep > 0) ? inputStep : 1;
        const step = e.shiftKey ? 10 : baseStep; // Usa 10mm con Shift, sino el valor del input o 1

        if (e.key === 'ArrowLeft') {
            e.preventDefault();
            apiJog(-step);
        } else if (e.key === 'ArrowRight') {
            e.preventDefault();
            apiJog(step);
        } else if (e.key === 'Home') {
            e.preventDefault();
            apiHome();
        }
    });

    // --- 4. Setup Realtime Connection ---
    setupRealtime(); // Intenta conectar Socket.IO

    // --- 5. Initial Data Load & Periodic Refresh ---
    console.log("Performing initial data load...");
    fetchAndRenderConfig(); // Carga la configuración primero
    refreshAll(); // Carga todos los datos y renderiza
    setInterval(refreshAll, 15000); // Refresco periódico (más largo, ya que tenemos Socket.IO)

    // --- 6. Service Worker Registration ---
    if ('serviceWorker' in navigator) {
        window.addEventListener('load', () => {
            navigator.serviceWorker.register('./sw.js')
                .then(reg => console.log('Service Worker registrado', reg.scope))
                .catch(err => console.log('Error registrando Service Worker', err));
        });
    }

    // --- 7. Add CSS for animations ---
    const style = document.createElement('style');
    style.textContent = `
        @keyframes slideIn {
            from { opacity: 0; transform: translateY(15px) scale(0.98); }
            to { opacity: 1; transform: translateY(0) scale(1); }
        }
        #events-table-body tr { opacity: 0; animation: slideIn 0.5s ease-out forwards; } /* Apply only to rows initially */

        .loader {
            width: 1em; height: 1em; border: 2px solid currentColor;
            border-top-color: transparent; border-radius: 50%;
            animation: spin 1s linear infinite; display: inline-block;
            vertical-align: text-bottom; margin-right: 0.5em;
        }
        @keyframes spin { to { transform: rotate(360deg); } }
    `;
    document.head.appendChild(style);

    console.log("Inicialización completa.");
}); // Fin de DOMContentLoaded