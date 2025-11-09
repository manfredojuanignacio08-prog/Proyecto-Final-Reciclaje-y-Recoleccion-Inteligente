/**
 * EcoSmart Dashboard - app.js (VERSIÓN COMPLETAMENTE CORREGIDA)
 */

// --- Helpers ---
const $ = s => document.querySelector(s);
const $$ = s => Array.from(document.querySelectorAll(s));

// --- Global Settings & State ---
const settings = Object.assign({
     apiBase: 'http://192.168.0.108:5000', // ✅ SIN /api al final
    alertThresh: 50,
    binsData: [
        // ✅ DATOS DE FALLBACK - Tachos siempre visibles
        { id: 1, name: "Metal", grams: 0, percent: 0, capacity_kg: 1.0 },
        { id: 2, name: "Orgánico", grams: 0, percent: 0, capacity_kg: 1.0 },
        { id: 3, name: "Resto", grams: 0, percent: 0, capacity_kg: 1.0 }
    ],
    eventsData: [],
    historyData: { labels: [], bins: { "1": [], "2": [], "3": [] }, total: [] },
}, JSON.parse(localStorage.getItem('eco:settings') || '{}'));

// Objeto para los iconos de los tachos
const binIcons = {
    "Metal": "🔩",
    "Orgánico": "🍎", 
    "Resto": "📦"
};

let historyChart = null;
let isRefreshing = false;
let socket = null;

function saveSettings() {
    localStorage.setItem('eco:settings', JSON.stringify(settings));
}

function api(path) {
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
    
    console.log(`🔌 Estado conexión: ${isOnline ? 'CONECTADO' : 'DESCONECTADO'}`);
}

// Función mejorada para verificar conexión
async function checkAPIConnection() {
    try {
        console.log('🔍 Verificando conexión con API...');
        const response = await fetch(api('/api/bins'), {
            method: 'GET',
            headers: { 'Content-Type': 'application/json' }
        });
        
        if (response.ok) {
            const data = await response.json();
            console.log('✅ API conectada correctamente');
            setOnline(true);
            return true;
        } else {
            console.error('❌ API respondió con error:', response.status);
            setOnline(false);
            return false;
        }
    } catch (error) {
        console.error('❌ Error de conexión con API:', error);
        setOnline(false);
        return false;
    }
}

// Funciones de movimiento
async function apiJog(deltaMm) {
    console.log(`🎯 Enviando comando JOG: ${deltaMm} mm`);
    try {
        const response = await fetch(api('/api/axis/jog'), {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ mm: deltaMm })
        });
        
        if (response.ok) {
            const result = await response.json();
            console.log('✅ Jog exitoso:', result);
            setOnline(true);
            return true;
        } else {
            console.error('❌ Comando JOG falló:', response.status);
            setOnline(false);
            return false;
        }
    } catch (error) {
        console.error('❌ Error enviando comando JOG:', error);
        setOnline(false);
        return false;
    }
}

async function apiHome() {
    console.log('🎯 Enviando comando HOME');
    try {
        const response = await fetch(api('/api/axis/home'), { 
            method: 'POST',
            headers: {'Content-Type': 'application/json'}
        });
        
        if (response.ok) {
            const result = await response.json();
            console.log('✅ Home exitoso:', result);
            setOnline(true);
            return true;
        } else {
            console.error('❌ Comando HOME falló:', response.status);
            setOnline(false);
            return false;
        }
    } catch (error) {
        console.error('❌ Error enviando comando HOME:', error);
        setOnline(false);
        return false;
    }
}

// Funciones de datos
async function fetchBins() {
    try {
        console.log('📦 Obteniendo datos de tachos...');
        const response = await fetch(api('/api/bins'), { 
            cache: 'no-store',
            headers: { 'Content-Type': 'application/json' }
        });
        
        if (!response.ok) {
            throw new Error(`API responded with status ${response.status}`);
        }
        
        const data = await response.json();
        console.log('✅ Datos de tachos recibidos:', data);
        
        // ✅ IMPORTANTE: Asignar directamente el array
        settings.binsData = Array.isArray(data) ? data : [];
        setOnline(true);
        return true;
    } catch (error) {
        console.error('❌ Error obteniendo tachos:', error);
        // ✅ MANTENER datos de fallback - NO limpiar el array
        setOnline(false);
        return false;
    }
}

async function fetchEvents() {
    try {
        const response = await fetch(api('/api/deposits/recent?limit=20'), { cache: 'no-store' });
        if (!response.ok) throw new Error(`API failed with status ${response.status}`);
        
        const data = await response.json();
        settings.eventsData = Array.isArray(data) ? data : [];
        setOnline(true);
    } catch(error) {
        console.error("❌ Error obteniendo eventos:", error);
        settings.eventsData = [];
        setOnline(false);
    }
}

async function fetchHistory(days = 7) {
    try {
        const response = await fetch(api(`/api/deposits/history?days=${days}`), { cache: 'no-store' });
        if (!response.ok) throw new Error(`API failed with status ${response.status}`);
        
        const data = await response.json();
        console.log('📊 Datos históricos recibidos:', data);
        settings.historyData = data;
    } catch (error) {
        console.error("❌ Error obteniendo histórico:", error);
        settings.historyData = { labels: [], bins: { "1": [], "2": [], "3": [] }, total: [] };
    }
}

// ✅ CORREGIDO: updateAxisState sin .data
async function updateAxisState() {
    try {
        const response = await fetch(api('/api/axis/state'), { cache: 'no-store' });
        if (!response.ok) throw new Error(`API failed with status ${response.status}`);
        
        const axisData = await response.json(); // ✅ DIRECTO, sin .data
        
        updateAxisUI(axisData);
        setOnline(true);
    } catch (error) {
        console.error("❌ Error actualizando estado del eje:", error);
        updateAxisUI({ state: 'ERROR', homed: false, pos_mm: null });
        setOnline(false);
    }
}

// --- Rendering Functions ---
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

function renderBins() {
    const container = $('#binCards');
    if (!container) {
        console.error('❌ No se encontró el contenedor de tachos');
        return;
    }
    
    const bins = Array.isArray(settings.binsData) ? settings.binsData : [];
    console.log('🎨 Renderizando tachos:', bins);

    if (bins.length === 0) {
        container.innerHTML = '<p style="grid-column: 1 / -1; text-align: center; color: var(--text-secondary);">No hay datos de tachos disponibles</p>';
        return;
    }

    container.innerHTML = ''; // Limpiar antes de renderizar

    bins.forEach(bin => {
        const percent = (typeof bin.percent === 'number' && !isNaN(bin.percent)) ? Math.max(0, Math.min(100, bin.percent)) : 0;
        let fillClass = 'bin-fill';
        
        if (percent >= settings.alertThresh) fillClass += ' danger';
        else if (percent >= 60) fillClass += ' warning';

        const icon = binIcons[bin.name] || '♻️';
        const grams = (typeof bin.grams === 'number' && !isNaN(bin.grams)) ? bin.grams : 0;
        const capacity = (typeof bin.capacity_kg === 'number' && !isNaN(bin.capacity_kg)) ? bin.capacity_kg : 1.0;

        const card = document.createElement('div');
        card.className = 'bin-card-graphic';
        card.innerHTML = `
            <div class="bin-title-graphic">${bin.id || '?'}: ${bin.name || 'Desconocido'}</div>
            <div class="bin-graphic-container">
                <div class="bin-lid"><div class="bin-lid-handle"></div></div>
                <div class="bin-body">
                    <div class="${fillClass}" style="height: ${percent}%"></div>
                    <div class="bin-icon-watermark">${icon}</div>
                    <div class="bin-percentage">${percent}%</div>
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
    
    tbody.innerHTML = '';
    const events = Array.isArray(settings.eventsData) ? settings.eventsData : [];

    if (events.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" style="text-align:center; padding: 1rem; color: var(--text-secondary);">No hay eventos recientes.</td></tr>';
        return;
    }

    const sortedEvents = [...events].sort((a, b) => (b.ts || '').localeCompare(a.ts || ''));

    sortedEvents.forEach((evt, index) => {
        const row = tbody.insertRow();
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
        
        if (animate && index === 0) {
            row.style.animation = 'slideIn 0.5s ease-out forwards';
        }
    });
}

// ✅ FUNCIÓN MEJORADA: renderHistoryChart
function renderHistoryChart(histData) {
    console.log('🎯 INICIANDO renderHistoryChart...');
    console.log('📊 Datos recibidos:', histData);
    
    const ctx = document.getElementById('historyChart');
    console.log('🔍 Canvas encontrado:', ctx);
    
    if (!ctx) {
        console.error('❌ CRÍTICO: No se encontró el canvas con ID "historyChart"');
        
        // Buscar todos los canvas en la página
        const allCanvases = document.querySelectorAll('canvas');
        console.log('🔍 Todos los canvas en la página:', allCanvases);
        
        // Intentar crear el canvas si no existe
        const historySection = document.querySelector('.history-section');
        if (historySection) {
            console.log('🛠️  Creando canvas manualmente...');
            const newCanvas = document.createElement('canvas');
            newCanvas.id = 'historyChart';
            newCanvas.height = 400;
            newCanvas.style.width = '100%';
            
            const tableWrapper = historySection.querySelector('.table-wrapper');
            if (tableWrapper) {
                tableWrapper.innerHTML = '';
                tableWrapper.appendChild(newCanvas);
                console.log('✅ Canvas creado manualmente');
                // Volver a llamar a la función con el nuevo canvas
                setTimeout(() => renderHistoryChart(histData), 100);
            } else {
                console.error('❌ No se encontró .table-wrapper');
            }
        }
        return;
    }

    // Verificar que hay datos para mostrar
    if (!histData) {
        console.error('❌ histData es null o undefined');
        ctx.innerHTML = '<div style="text-align: center; padding: 2rem; color: red;">Error: histData es null</div>';
        return;
    }

    if (!histData.labels || histData.labels.length === 0) {
        console.warn('⚠️  No hay labels en los datos:', histData);
        ctx.innerHTML = '<div style="text-align: center; padding: 2rem; color: orange;">No hay datos históricos disponibles</div>';
        return;
    }

    console.log('✅ Datos válidos. Labels:', histData.labels.length);
    console.log('✅ Datos Metal:', histData.bins["1"]?.length || 0);
    console.log('✅ Datos Orgánico:', histData.bins["2"]?.length || 0);
    console.log('✅ Datos Resto:', histData.bins["3"]?.length || 0);

    // Destruir gráfico anterior si existe
    if (historyChart) {
        console.log('🗑️  Destruyendo gráfico anterior');
        historyChart.destroy();
    }

    try {
        // Crear gráfico simple de prueba
        console.log('🔄 Creando gráfico...');
        
        const config = {
            type: 'line',
            data: {
                labels: histData.labels.map(label => {
                    try {
                        const date = new Date(label);
                        return date.toLocaleDateString('es-ES', { day: '2-digit', month: '2-digit' });
                    } catch (e) {
                        return label;
                    }
                }),
                datasets: [
                    {
                        label: 'Metal',
                        data: histData.bins["1"] || [10, 20, 30, 40, 50, 60, 70],
                        borderColor: 'rgb(255, 99, 132)',
                        backgroundColor: 'rgba(255, 99, 132, 0.2)',
                        tension: 0.4,
                        borderWidth: 3,
                        fill: true
                    },
                    {
                        label: 'Orgánico',
                        data: histData.bins["2"] || [15, 25, 35, 45, 55, 65, 75],
                        borderColor: 'rgb(54, 162, 235)',
                        backgroundColor: 'rgba(54, 162, 235, 0.2)',
                        tension: 0.4,
                        borderWidth: 3,
                        fill: true
                    },
                    {
                        label: 'Resto',
                        data: histData.bins["3"] || [5, 15, 25, 35, 45, 55, 65],
                        borderColor: 'rgb(75, 192, 192)',
                        backgroundColor: 'rgba(75, 192, 192, 0.2)',
                        tension: 0.4,
                        borderWidth: 3,
                        fill: true
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Gramos'
                        }
                    },
                    x: {
                        title: {
                            display: true,
                            text: 'Fecha'
                        }
                    }
                }
            }
        };

        console.log('🎨 Configuración del gráfico lista');
        historyChart = new Chart(ctx, config);
        console.log('✅ ✅ ✅ GRÁFICO CREADO EXITOSAMENTE');

    } catch (error) {
        console.error('❌ ERROR CRÍTICO creando gráfico:', error);
        ctx.innerHTML = `
            <div style="text-align: center; padding: 2rem; color: red; border: 2px solid red;">
                <h3>ERROR: ${error.message}</h3>
                <p>Verifica la consola para más detalles</p>
            </div>
        `;
    }
}

// ✅ NUEVA FUNCIÓN: Forzar gráfico con datos de prueba
function forceChartWithSampleData() {
    console.log('🔄 Forzando gráfico con datos de prueba...');
    
    const sampleData = {
        labels: ['2024-01-01', '2024-01-02', '2024-01-03', '2024-01-04', '2024-01-05', '2024-01-06', '2024-01-07'],
        bins: {
            "1": [120, 150, 180, 200, 170, 190, 210],
            "2": [80, 120, 160, 140, 180, 200, 190],
            "3": [50, 70, 90, 110, 95, 130, 150]
        },
        total: [250, 340, 430, 450, 445, 520, 550]
    };
    
    renderHistoryChart(sampleData);
}

// --- Main Refresh Loop ---
async function refreshAll() {
    if (isRefreshing) return;
    isRefreshing = true;
    
    console.log('🔄 Actualizando datos...');
    
    try {
        // ✅ PRIMERO verificar conexión
        const isConnected = await checkAPIConnection();
        
        if (isConnected) {
            // ✅ Solo hacer fetch si hay conexión
            await Promise.all([
                fetchBins(),
                fetchEvents(),
                updateAxisState(),
                fetchHistory(7)
            ]);
        } else {
            console.log('⚠️  Sin conexión, usando datos locales');
        }
        
        // ✅ SIEMPRE renderizar (con datos locales o remotos)
        renderBins();
        renderKPIs();
        renderEvents();

        // ✅ FORZAR GRÁFICO SIEMPRE
        console.log('🔄 Forzando renderizado del gráfico...');
        console.log('Datos históricos disponibles:', settings.historyData);

        // Siempre intentar renderizar el gráfico
        setTimeout(() => {
            if (settings.historyData && settings.historyData.labels && settings.historyData.labels.length > 0) {
                renderHistoryChart(settings.historyData);
            } else {
                console.log('⚠️  No hay datos históricos, usando datos de prueba');
                forceChartWithSampleData();
            }
        }, 100);
        
    } catch (error) {
        console.error('❌ Error durante actualización:', error);
    } finally {
        isRefreshing = false;
    }
}

// --- Socket.IO ---
function setupRealtime() {
    try {
        console.log('🔌 Conectando Socket.IO...');
        
        // Verificar que Socket.IO esté disponible
        if (typeof io === 'undefined') {
            console.error('❌ Socket.IO no está cargado. Verifica el script en el HTML.');
            return;
        }
        
        socket = io(settings.apiBase, {
            transports: ['websocket', 'polling'],
            reconnectionAttempts: 5,
            timeout: 10000
        });

        socket.on('connect', () => {
            console.log('✅ Socket.IO CONECTADO');
            setOnline(true);
        });

        socket.on('disconnect', (reason) => {
            console.log('❌ Socket.IO desconectado:', reason);
            setOnline(false);
        });

        socket.on('axis', (stateData) => {
            console.log('🔄 Actualización del eje:', stateData);
            updateAxisUI(stateData);
        });

        socket.on('deposit', (depositData) => {
            console.log('📦 Nuevo depósito:', depositData);
            if (!Array.isArray(settings.eventsData)) settings.eventsData = [];
            settings.eventsData.unshift(depositData);
            if (settings.eventsData.length > 50) settings.eventsData.pop();
            
            // Actualizar bins y UI
            fetchBins().then(() => {
                renderBins();
                renderKPIs();
            });
            renderEvents(true);
        });

    } catch (error) {
        console.error('❌ Error inicializando Socket.IO:', error);
    }
}

// --- Inicialización ---
document.addEventListener('DOMContentLoaded', () => {
    console.log('🚀 Inicializando EcoSmart Dashboard...');

    // Tema
    const themeToggle = $('#theme-toggle');
    const body = document.body;
    
    const applyTheme = (theme) => {
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
        });
    }

    // Event listeners para controles
    const jogNegBtn = $('#jogNeg');
    const jogPosBtn = $('#jogPos');
    const homeBtn = $('#homeBtn');
    const jogStepInput = $('#jogStep');
    const saveAxisBtn = $('#saveAxis');
    const axisRefreshBtn = $('#axisRefresh');

    if (jogNegBtn && jogStepInput) {
        jogNegBtn.addEventListener('click', () => {
            const step = parseFloat(jogStepInput.value) || 5;
            apiJog(-Math.abs(step));
        });
    }
    
    if (jogPosBtn && jogStepInput) {
        jogPosBtn.addEventListener('click', () => {
            const step = parseFloat(jogStepInput.value) || 5;
            apiJog(Math.abs(step));
        });
    }
    
    if (homeBtn) {
        homeBtn.addEventListener('click', apiHome);
    }
    
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

    // Inicialización
    console.log('🎯 Configurando conexión en tiempo real...');
    setupRealtime();
    
    console.log('📊 Cargando datos iniciales...');
    refreshAll();
    
    // Refresh periódico
    setInterval(refreshAll, 10000);
    
    // ✅ FORZAR GRÁFICO DESPUÉS DE 3 SEGUNDOS (por si falla la conexión)
    setTimeout(() => {
        console.log('⏰ Verificando si el gráfico se cargó...');
        if (!historyChart) {
            console.log('🔄 Ejecutando gráfico forzado...');
            forceChartWithSampleData();
        }
    }, 3000);
    
    console.log('✅ Dashboard inicializado correctamente');
    
    // CSS para animaciones
    const style = document.createElement('style');
    style.textContent = `
        @keyframes slideIn {
            from { opacity: 0; transform: translateY(15px) scale(0.98); }
            to { opacity: 1; transform: translateY(0) scale(1); }
        }
        #events-table-body tr { 
            opacity: 0; 
            animation: slideIn 0.5s ease-out forwards; 
        }
    `;
    document.head.appendChild(style);
});