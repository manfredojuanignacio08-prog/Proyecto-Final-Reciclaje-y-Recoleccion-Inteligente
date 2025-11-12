const char PAGINA1CODE[] = 
R"=====(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>EcoSmart Dashboard</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Poppins:wght@400;600;700&family=Roboto:wght@400;500;700&display=swap" rel="stylesheet">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="https://cdn.socket.io/4.7.2/socket.io.min.js"></script>
    <style>
/* --- VARIABLES Y ESTILOS GLOBALES --- */
:root {
    --green-light: #61b46b;
    --yellow-accent: #ffc107;
    --red-danger: #e57373;
    --blue-info: #64b5f6;
    --bg-light: #f4f7f6;
    --card-bg-light: #ffffff;
    --text-light: #212121;
    --border-light: #e0e0e0;
    --shadow-light: 0 4px 20px rgba(0, 0, 0, 0.05);
    --bin-empty-color: #e0e0e0;
}

body.dark-mode {
    --green-light: #61b46b;
    --yellow-accent: #ffc107;
    --red-danger: #ef5350;
    --blue-info: #42a5f5;
    --bg-light: #121212;
    --card-bg-light: #1e1e1e;
    --text-light: #e0e0e0;
    --border-light: #333333;
    --shadow-light: 0 4px 20px rgba(0, 0, 0, 0.2);
    --bin-empty-color: #424242;
}

* {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
}

body {
    font-family: 'Roboto', sans-serif;
    background-color: var(--bg-light);
    color: var(--text-light);
    transition: background-color 0.3s, color 0.3s;
}

/* --- HEADER --- */
header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 1rem 2rem;
    background-color: var(--card-bg-light);
    border-bottom: 1px solid var(--border-light);
    box-shadow: var(--shadow-light);
    position: sticky;
    top: 0;
    z-index: 100;
    transition: background-color 0.3s, border-color 0.3s;
}
.logo-container { display: flex; align-items: center; gap: 1rem; }
.logo { height: 40px; }
header h1 { font-family: 'Poppins', sans-serif; font-size: 1.5rem; color: var(--text-light); }
.header-actions { display: flex; align-items: center; gap: 1rem; }

.presentation-link {
    color: var(--text-light);
    padding: 6px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: all 0.3s ease;
}
.presentation-link:hover {
    background-color: var(--border-light);
    color: var(--green-light);
}

.theme-button {
    background: none; border: 2px solid var(--border-light); color: var(--text-light);
    width: 36px; height: 36px; border-radius: 50%; font-size: 1.2rem;
    cursor: pointer; display: flex; justify-content: center; align-items: center;
    transition: all 0.3s ease;
}
.theme-button:hover {
    transform: scale(1.1) rotate(15deg);
    border-color: var(--green-light);
    color: var(--green-light);
}

.status-dot {
    width: 12px; height: 12px; border-radius: 50%;
    transition: background-color 0.5s ease;
}
.status-dot.connected {
    background-color: var(--green-light);
    animation: pulse-connected 2s infinite;
}
.status-dot.disconnected {
    background-color: var(--red-danger);
    animation: pulse-disconnected 2s infinite;
}
@keyframes pulse-connected {
    0% { box-shadow: 0 0 0 0 rgba(97, 180, 107, 0.7); }
    70% { box-shadow: 0 0 0 10px rgba(97, 180, 107, 0); }
    100% { box-shadow: 0 0 0 0 rgba(97, 180, 107, 0); }
}
@keyframes pulse-disconnected {
    0% { box-shadow: 0 0 0 0 rgba(229, 115, 115, 0.7); }
    70% { box-shadow: 0 0 0 10px rgba(229, 115, 115, 0); }
    100% { box-shadow: 0 0 0 0 rgba(229, 115, 115, 0); }
}

/* --- LAYOUT PRINCIPAL MEJORADO --- */
main {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 1.5rem;
    padding: 1.5rem;
    max-width: 1600px;
    margin: 0 auto;
}

.card {
    background-color: var(--card-bg-light);
    border-radius: 12px;
    padding: 1.5rem;
    box-shadow: var(--shadow-light);
    border: 1px solid var(--border-light);
    transition: all 0.3s ease;
}

.card h2 {
    font-family: 'Poppins', sans-serif;
    font-size: 1.2rem;
    margin-bottom: 1.5rem;
    border-bottom: 2px solid var(--green-light);
    padding-bottom: 0.5rem;
    display: flex;
    align-items: center;
    gap: 0.5rem;
}
.card h2 .icon { font-size: 1.4rem; }

/* Grid mejorado: 3 columnas */
.grid-bins {
    grid-column: 1 / 4;
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 1.5rem;
}

/* Columna derecha para KPIs y controles */
.sidebar-column {
    grid-column: 3 / 4;
    grid-row: 2 / 4;
    display: flex;
    flex-direction: column;
    gap: 1.5rem;
}

/* Eventos ocupa 2 columnas */
.events-section {
    grid-column: 1 / 3;
    grid-row: 2 / 3;
}

/* Config ocupa 2 columnas */
.config-section {
    grid-column: 1 / 3;
    grid-row: 3 / 4;
}

/* Histórico ocupa todo el ancho */
.history-section {
    grid-column: 1 / 4;
    grid-row: 4 / 5;
}

/* --- DISEÑO DE TACHOS --- */
.bin-card-graphic {
    background-color: var(--card-bg-light);
    border-radius: 12px;
    box-shadow: var(--shadow-light);
    border: 1px solid var(--border-light);
    padding: 1.5rem;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 1rem;
}
.bin-title-graphic {
    font-family: 'Poppins', sans-serif;
    font-size: 1.3rem;
    font-weight: 600;
}
.bin-graphic-container {
    width: 120px;
    height: 180px;
    position: relative;
    margin-bottom: 1rem;
}
.bin-body {
    width: 100%;
    height: 100%;
    background-color: var(--bin-empty-color);
    border-radius: 6px 6px 15px 15px;
    position: relative;
    overflow: hidden;
    border: 3px solid var(--border-light);
}
.bin-lid {
    width: 130px;
    height: 12px;
    background-color: #9e9e9e;
    border-radius: 6px 6px 0 0;
    position: absolute;
    top: -12px;
    left: -5px;
    border: 3px solid var(--border-light);
    border-bottom: none;
}
.bin-lid-handle {
    width: 40px;
    height: 6px;
    background-color: #757575;
    border-radius: 3px;
    position: absolute;
    top: -6px;
    left: 50%;
    transform: translateX(-50%);
}
.bin-fill {
    position: absolute;
    bottom: 0;
    left: 0;
    width: 100%;
    background-color: var(--green-light);
    transition: height 0.5s ease-out, background-color 0.5s ease;
}
.bin-fill.warning { background-color: var(--yellow-accent); }
.bin-fill.danger { background-color: var(--red-danger); }
.bin-icon-watermark {
    font-size: 4rem;
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    opacity: 0.1;
    color: var(--text-light);
}
body.dark-mode .bin-icon-watermark { opacity: 0.2; }
.bin-percentage {
    font-family: 'Poppins', sans-serif;
    font-size: 2.5rem;
    font-weight: 700;
    color: var(--text-light);
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    text-shadow: 0 0 8px var(--bg-light);
}
.bin-details {
    width: 100%;
    display: flex;
    justify-content: space-between;
    font-size: 0.9rem;
    font-weight: 500;
}
.bin-weight-graphic { font-size: 1.1rem; color: var(--green-light); }

/* --- KPIs COMPACTOS --- */
.kpi-grid { 
    display: grid; 
    grid-template-columns: 1fr; 
    gap: 1rem; 
}
.kpi-item { 
    display: flex; 
    flex-direction: column;
    padding: 0.8rem;
    background: var(--bg-light);
    border-radius: 8px;
}
.kpi-value {
    font-family: 'Poppins', sans-serif;
    font-size: 1.8rem;
    font-weight: 700;
    color: var(--green-light);
}
.kpi-label { 
    font-size: 0.85rem; 
    color: #888; 
    text-transform: uppercase;
    margin-top: 0.25rem;
}
body.dark-mode .kpi-label { color: #aaa; }

/* --- ESTADO DEL SISTEMA COMPACTO --- */
.axis-info { 
    display: flex; 
    flex-direction: column; 
    gap: 0.8rem; 
    margin-bottom: 1rem;
}
.axis-item {
    display: grid;
    grid-template-columns: auto 1fr auto;
    align-items: center;
    font-size: 0.9rem;
    gap: 0.5rem;
    padding: 0.5rem;
    background: var(--bg-light);
    border-radius: 6px;
}
.axis-item .icon { font-size: 1.2rem; }
.axis-label { font-weight: 500; color: #888; }
body.dark-mode .axis-label { color: #aaa; }
.axis-value { font-family: 'Poppins', sans-serif; font-weight: 600; }
.status-text.status-idle { color: var(--green-light); }
.status-text.status-moving { color: var(--blue-info); }
.status-text.status-error, .status-text.status-unknown { color: var(--red-danger); }

/* --- CONTROLES DEL EJE COMPACTOS --- */
.axis-controls{
    display: grid;
    grid-template-columns: auto 1fr auto;
    gap: 0.5rem;
    margin: 1rem 0;
    align-items: center;
}
.axis-controls-row {
    display: flex;
    gap: 0.5rem;
    margin-bottom: 0.5rem;
}
.btn-jog, .btn-home{
    padding: 0.5rem 0.8rem;
    border: none;
    border-radius: 8px;
    background: #61b46b;
    color: #fff;
    font-weight: 600;
    font-size: 0.85rem;
    cursor: pointer;
    transition: transform .15s ease, opacity .15s ease;
}
.btn-jog:hover, .btn-home:hover{ transform: translateY(-1px); opacity: .95; }
.jog-input{
    width: 4rem;
    padding: 0.5rem;
    border: 1px solid var(--border-light);
    border-radius: 8px;
    background: var(--bg-light);
    color: var(--text-light);
    text-align: center;
    font-size: 0.9rem;
}
.axis-help{ 
    display: block; 
    margin-top: 0.5rem; 
    font-size: 0.75rem;
    opacity: 0.75; 
    line-height: 1.3;
}

.refresh-button {
    width: 100%;
    padding: 0.7rem;
    margin-top: 0.5rem;
    border: none;
    border-radius: 8px;
    background-color: var(--green-light);
    color: #fff;
    font-family: 'Poppins', sans-serif;
    font-size: 0.9rem;
    font-weight: 600;
    cursor: pointer;
    transition: background-color 0.3s, transform 0.2s;
}
.refresh-button:hover {
    background-color: #55a35a;
    transform: translateY(-2px);
}

/* --- TABLA DE EVENTOS COMPACTA --- */
.table-wrapper { width: 100%; overflow-x: auto; }
table { width: 100%; border-collapse: collapse; }
th, td { padding: 0.7rem 0.8rem; text-align: left; border-bottom: 1px solid var(--border-light); font-size: 0.9rem; }
th {
    font-family: 'Poppins', sans-serif;
    font-size: 0.85rem;
    font-weight: 600;
    background-color: var(--bg-light);
}
tbody tr:hover { background-color: rgba(0, 0, 0, 0.02); }
body.dark-mode tbody tr:hover { background-color: rgba(255, 255, 255, 0.05); }

/* --- TAGS DE MATERIALES --- */
.tag {
    padding: 0.2rem 0.6rem;
    border-radius: 12px;
    font-size: 0.75rem;
    font-weight: 700;
    color: #fff;
    white-space: nowrap;
}
.tag-metal { background-color: #78909c; }
.tag-orgánico { background-color: #8d6e63; }
.tag-resto { background-color: #ab47bc; }

/* --- FORMULARIO DE CONFIGURACIÓN COMPACTO --- */
.config-form {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 1rem;
}
.form-group { display: flex; flex-direction: column; }
.form-group label { margin-bottom: 0.4rem; font-size: 0.85rem; font-weight: 500; }
.form-group input {
    padding: 0.6rem;
    border: 1px solid var(--border-light);
    border-radius: 8px;
    background-color: var(--bg-light);
    color: var(--text-light);
    font-size: 0.9rem;
}
.form-group-positions { 
    grid-column: 1 / -1; 
    display: grid; 
    grid-template-columns: repeat(3, 1fr); 
    gap: 1rem; 
}
.save-button {
    grid-column: 1 / -1;
    padding: 0.8rem;
    border: none;
    border-radius: 8px;
    background-color: var(--green-light);
    color: #fff;
    font-family: 'Poppins', sans-serif;
    font-size: 0.95rem;
    font-weight: 600;
    cursor: pointer;
    transition: background-color 0.3s, transform 0.2s;
}
.save-button:hover {
    background-color: #55a35a;
    transform: translateY(-2px);
}

/* Control manual mejorado */
.jog-card-compact {
    padding: 1rem;
}
.jog-grid {
    display: grid;
    grid-template-columns: repeat(5, 1fr);
    gap: 0.5rem;
    margin-bottom: 0.5rem;
}
.jog-grid small {
    grid-column: 1 / -1;
    text-align: center;
    font-size: 0.75rem;
    opacity: 0.75;
    margin-top: 0.25rem;
}

/* --- RESPONSIVE DESIGN --- */
@media (max-width: 1200px) {
    main { 
        grid-template-columns: repeat(2, 1fr); 
    }
    .grid-bins { 
        grid-column: 1 / 3; 
        grid-template-columns: repeat(3, 1fr); 
    }
    .sidebar-column {
        grid-column: 1 / 3;
        grid-row: auto;
        display: grid;
        grid-template-columns: repeat(2, 1fr);
        gap: 1.5rem;
    }
    .events-section {
        grid-column: 1 / 3;
        grid-row: auto;
    }
    .config-section {
        grid-column: 1 / 3;
        grid-row: auto;
    }
    .history-section {
        grid-column: 1 / 3;
    }
}

@media (max-width: 768px) {
    main { 
        grid-template-columns: 1fr; 
        padding: 1rem; 
    }
    header { padding: 1rem; }
    header h1 { font-size: 1.2rem; }
    .grid-bins { 
        grid-column: 1 / -1; 
        grid-template-columns: 1fr; 
    }
    .sidebar-column {
        grid-column: 1 / -1;
        grid-template-columns: 1fr;
    }
    .events-section,
    .config-section,
    .history-section {
        grid-column: 1 / -1;
    }
    .config-form {
        grid-template-columns: 1fr;
    }
    .form-group-positions { 
        grid-template-columns: 1fr; 
    }
    .jog-grid {
        grid-template-columns: repeat(3, 1fr);
    }
    .jog-grid .btn-home {
        grid-column: 2 / 3;
    }
}
    </style>
</head>
<body>
    <header>
        <div class="logo-container">
            <div class="logo">🌱</div>
            <div>
                <h1>EcoSmart Dashboard</h1>
                <div id="ip-info" style="font-size: 0.8rem; color: #888; margin-top: 0.2rem;">
                    IP: <span id="current-ip">Cargando...</span> | 
                    mDNS: <a href="http://pro.local" style="color: #61b46b;">pro.local</a>
                </div>
            </div>
        </div>
        <div class="header-actions">
            <a href="/presentacion" class="presentation-link" title="Ir a la Presentación del Proyecto">
                <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor" width="24" height="24">
                    <path d="M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8h5z"/>
                </svg>
            </a>
            <span id="status-indicator" class="status-dot disconnected" title="Estado de la API"></span>
            <button id="theme-toggle" class="theme-button">☀️</button>
        </div>
    </header>

    <main>
        <!-- Tachos (fila 1, 3 columnas) -->
        <section id="binCards" class="grid-bins">
            <!-- Los tachos se generarán dinámicamente -->
        </section>

        <!-- Eventos (fila 2, 2 columnas) -->
        <section id="events" class="card events-section">
            <h2><span class="icon">📋</span> Eventos Recientes</h2>
            <div class="table-wrapper">
                <table>
                    <thead>
                        <tr>
                            <th>Timestamp</th>
                            <th>Tacho</th>
                            <th>Material</th>
                            <th>Peso (g)</th>
                            <th>Llenado (%)</th>
                        </tr>
                    </thead>
                    <tbody id="events-table-body">
                        <tr><td colspan="5" style="text-align: center;">Cargando datos...</td></tr>
                    </tbody>
                </table>
            </div>
        </section>

        <!-- Sidebar derecha: KPIs, Estado, Controles -->
        <div class="sidebar-column">
            <!-- KPIs -->
            <section id="kpi" class="card">
                <h2><span class="icon">📊</span> KPIs</h2>
                <div class="kpi-grid">
                    <div class="kpi-item">
                        <span class="kpi-value" id="totalWeight">--</span>
                        <span class="kpi-label">Peso Total (g)</span>
                    </div>
                    <div class="kpi-item">
                        <span class="kpi-value" id="totalItems">--</span>
                        <span class="kpi-label">Items</span>
                    </div>
                    <div class="kpi-item">
                        <span class="kpi-value" id="mostFrequent">--</span>
                        <span class="kpi-label">Frecuente</span>
                    </div>
                </div>
                <button id="taraButton" class="refresh-button">Realizar Tara</button>
            </section>

            <!-- Estado del Sistema + Controles -->
            <section id="axisCard" class="card">
                <h2><span class="icon">⚙️</span> Sistema & Control</h2>
                <div class="axis-info">
                    <div class="axis-item">
                        <span class="icon">🤖</span>
                        <span class="axis-label">Estado:</span>
                        <span id="axisState" class="axis-value status-text status-unknown">DESCONOCIDO</span>
                    </div>
                    <div class="axis-item">
                        <span class="icon">🏠</span>
                        <span class="axis-label">Homed:</span>
                        <span id="axisHome" class="axis-value">--</span>
                    </div>
                    <div class="axis-item">
                        <span class="icon">📍</span>
                        <span class="axis-label">Posición:</span>
                        <span id="axisPos" class="axis-value">-- mm</span>
                    </div>
                </div>
                
                <div class="axis-controls">
                    <button id="jogNeg" class="btn-jog" title="Mover -5 mm">⟵ −</button>
                    <input id="jogStep" type="number" step="0.5" value="5" class="jog-input" aria-label="Paso">
                    <button id="jogPos" class="btn-jog" title="Mover +5 mm">+ ⟶</button>
                </div>
                
                <button id="homeBtn" class="btn-home" style="width: 100%;">🏠 Hacer Homing</button>
                <button id="axisRefresh" class="refresh-button">Actualizar</button>
                
                <small class="axis-help">Cambiá el paso y usá ± para mover. Teclas ←/→: ±1mm, Shift+←/→: ±10mm</small>
            </section>

            <!-- Control Manual Jog -->
            <section id="jogCard" class="card jog-card-compact">
                <h2><span class="icon">🎮</span> Jog Manual</h2>
                <div class="jog-grid">
                    <button class="btn-jog" data-mm="-10">−10</button>
                    <button class="btn-jog" data-mm="-1">−1</button>
                    <button id="btnHome" class="btn-home">🏠</button>
                    <button class="btn-jog" data-mm="1">+1</button>
                    <button class="btn-jog" data-mm="10">+10</button>
                    <small>Atajos: ←/→ (±1mm) | Shift+←/→ (±10mm)</small>
                </div>
            </section>
        </div>

        <!-- Configuración (fila 3, 2 columnas) -->
        <section id="config" class="card config-section">
            <h2><span class="icon">🔧</span> Configuración del Eje</h2>
            <form id="axis-form" class="config-form">
                <div class="form-group">
                    <label for="stepsPerMm">Pasos/mm</label>
                    <input type="number" id="stepsPerMm" value="2.5" step="0.01">
                </div>
                <div class="form-group">
                    <label for="vmax">Velocidad Máx (mm/s)</label>
                    <input type="number" id="vmax" value="120">
                </div>
                <div class="form-group">
                    <label for="acc">Aceleración (mm/s²)</label>
                    <input type="number" id="acc" value="400">
                </div>
                <div class="form-group-positions">
                    <div class="form-group">
                        <label for="p1">Pos. Tacho 1 (mm)</label>
                        <input type="number" id="p1" value="0">
                    </div>
                    <div class="form-group">
                        <label for="p2">Pos. Tacho 2 (mm)</label>
                        <input type="number" id="p2" value="120">
                    </div>
                    <div class="form-group">
                        <label for="p3">Pos. Tacho 3 (mm)</label>
                        <input type="number" id="p3" value="240">
                    </div>
                </div>
                <button type="button" id="saveAxis" class="save-button">Guardar Configuración</button>
            </form>
        </section>

        <!-- Histórico (fila 4, ancho completo) -->
        <section id="history" class="card history-section">
            <h2><span class="icon">📈</span> Histórico (g por día)</h2>
            <div class="table-wrapper" style="height: 400px; position: relative;">
                <canvas id="historyChart" style="width: 100%; height: 100%;"></canvas>
            </div>
        </section>
    </main>

    <script>
        // Estado de la conexión
        let isConnected = false;
        let currentIP = "";
        const statusIndicator = document.getElementById('status-indicator');
        const currentIpElement = document.getElementById('current-ip');
        
        // Toggle tema oscuro
        const themeToggle = document.getElementById('theme-toggle');
        themeToggle.addEventListener('click', () => {
            document.body.classList.toggle('dark-mode');
            themeToggle.textContent = document.body.classList.contains('dark-mode') ? '🌙' : '☀️';
        });

        // Función para obtener información del sistema
        async function getSystemInfo() {
            try {
                const response = await fetch('/info');
                const data = await response.json();
                currentIP = data.ip;
                currentIpElement.textContent = currentIP;
                
                // Actualizar estado de conexión
                isConnected = true;
                statusIndicator.classList.remove('disconnected');
                statusIndicator.classList.add('connected');
                
            } catch (error) {
                console.error('Error fetching system info:', error);
                currentIpElement.textContent = 'No disponible';
                isConnected = false;
                statusIndicator.classList.remove('connected');
                statusIndicator.classList.add('disconnected');
            }
        }

        // Función para actualizar los pesos de las celdas de carga
        async function updateWeights() {
            try {
                const response = await fetch('/pesos');
                const data = await response.json();
                
                // Actualizar estado de conexión
                isConnected = true;
                statusIndicator.classList.remove('disconnected');
                statusIndicator.classList.add('connected');
                
                // Actualizar tachos
                updateBinCards(data);
                
                // Actualizar KPIs
                updateKPIs(data);
                
            } catch (error) {
                console.error('Error fetching weights:', error);
                isConnected = false;
                statusIndicator.classList.remove('connected');
                statusIndicator.classList.add('disconnected');
            }
        }

        // Función para actualizar los tachos
        function updateBinCards(data) {
            const binCards = document.getElementById('binCards');
            const bins = [
                { id: 1, name: 'Tacho 1', weight: data.peso1, material: 'Metal' },
                { id: 2, name: 'Tacho 2', weight: data.peso2, material: 'Orgánico' },
                { id: 3, name: 'Tacho 3', weight: data.peso3, material: 'Resto' }
            ];
            
            binCards.innerHTML = bins.map(bin => `
                <div class="bin-card-graphic">
                    <div class="bin-title-graphic">${bin.name}</div>
                    <div class="bin-graphic-container">
                        <div class="bin-lid">
                            <div class="bin-lid-handle"></div>
                        </div>
                        <div class="bin-body">
                            <div class="bin-fill" style="height: ${Math.min(bin.weight / 1000 * 100, 100)}%"></div>
                            <div class="bin-icon-watermark">🗑️</div>
                            <div class="bin-percentage">${Math.round(bin.weight / 1000 * 100)}%</div>
                        </div>
                    </div>
                    <div class="bin-details">
                        <span class="tag tag-${bin.material.toLowerCase()}">${bin.material}</span>
                        <span class="bin-weight-graphic">${bin.weight.toFixed(1)}g</span>
                    </div>
                </div>
            `).join('');
        }

        // Función para actualizar KPIs
        function updateKPIs(data) {
            const totalWeight = data.peso1 + data.peso2 + data.peso3;
            document.getElementById('totalWeight').textContent = totalWeight.toFixed(1);
            document.getElementById('totalItems').textContent = '3';
            document.getElementById('mostFrequent').textContent = 'Orgánico';
        }

        // Función para hacer tara
        async function performTara() {
            try {
                const response = await fetch('/tara');
                if (response.ok) {
                    alert('Tara realizada correctamente');
                    updateWeights();
                }
            } catch (error) {
                console.error('Error performing tara:', error);
                alert('Error al realizar la tara');
            }
        }

        // Inicializar gráfico histórico (placeholder)
        function initHistoryChart() {
            const ctx = document.getElementById('historyChart').getContext('2d');
            const historyChart = new Chart(ctx, {
                type: 'bar',
                data: {
                    labels: ['Lun', 'Mar', 'Mié', 'Jue', 'Vie', 'Sáb', 'Dom'],
                    datasets: [{
                        label: 'Peso Total (g)',
                        data: [1200, 1900, 3000, 5000, 2000, 3000, 4500],
                        backgroundColor: '#61b46b'
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    scales: {
                        y: {
                            beginAtZero: true
                        }
                    }
                }
            });
        }

        // === FUNCIONES PARA CONTROLES DEL EJE - ACTUALIZADAS ===
        async function moveAxis(direction, distance) {
            try {
                const response = await fetch(`/move?dir=${direction}&dist=${distance}`);
                const data = await response.json();
                console.log(`✅ Movimiento ejecutado:`, data);
                // Actualizar estado del eje después del movimiento
                updateAxisState();
            } catch (error) {
                console.error('❌ Error moviendo eje:', error);
                alert('Error al mover el eje');
            }
        }

        async function homeAxis() {
            try {
                const response = await fetch('/home');
                const data = await response.json();
                console.log('✅ Homing ejecutado:', data);
                // Actualizar estado después del homing
                updateAxisState();
            } catch (error) {
                console.error('❌ Error en homing:', error);
                alert('Error en homing del eje');
            }
        }

        async function updateAxisState() {
            try {
                // Esta función actualizaría el estado del eje (posición, estado, etc.)
                // Por ahora es un placeholder
                console.log('🔄 Actualizando estado del eje');
            } catch (error) {
                console.error('Error actualizando estado del eje:', error);
            }
        }

        async function saveAxisConfig() {
            try {
                const config = {
                    stepsPerMm: document.getElementById('stepsPerMm').value,
                    vmax: document.getElementById('vmax').value,
                    acc: document.getElementById('acc').value,
                    p1: document.getElementById('p1').value,
                    p2: document.getElementById('p2').value,
                    p3: document.getElementById('p3').value
                };
                
                const response = await fetch('/config', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                    },
                    body: JSON.stringify(config)
                });
                
                const data = await response.json();
                console.log('💾 Configuración guardada:', data);
                alert('Configuración guardada correctamente');
            } catch (error) {
                console.error('❌ Error guardando configuración:', error);
                alert('Error al guardar la configuración');
            }
        }

        // === CONFIGURACIÓN DE CONTROLES DEL EJE - ACTUALIZADA ===
        function setupAxisControls() {
            // Controles de movimiento principales
            const jogNegBtn = document.getElementById('jogNeg');
            const jogPosBtn = document.getElementById('jogPos');
            const homeBtn = document.getElementById('homeBtn');
            const jogStepInput = document.getElementById('jogStep');
            const axisRefreshBtn = document.getElementById('axisRefresh');
            
            if (jogNegBtn && jogStepInput) {
                jogNegBtn.addEventListener('click', () => {
                    const step = parseFloat(jogStepInput.value) || 5;
                    console.log(`🎯 Moviendo eje: -${step}mm`);
                    moveAxis('left', step);
                });
            }
            
            if (jogPosBtn && jogStepInput) {
                jogPosBtn.addEventListener('click', () => {
                    const step = parseFloat(jogStepInput.value) || 5;
                    console.log(`🎯 Moviendo eje: +${step}mm`);
                    moveAxis('right', step);
                });
            }
            
            if (homeBtn) {
                homeBtn.addEventListener('click', () => {
                    console.log('🎯 Ejecutando homing del eje');
                    homeAxis();
                });
            }
            
            if (axisRefreshBtn) {
                axisRefreshBtn.addEventListener('click', () => {
                    console.log('🔄 Actualizando estado del eje');
                    updateAxisState();
                });
            }
            
            // Controles manuales del jog card
            document.querySelectorAll('.btn-jog[data-mm]').forEach(btn => {
                btn.addEventListener('click', () => {
                    const mm = parseFloat(btn.getAttribute('data-mm'));
                    console.log(`🎮 Jog manual: ${mm}mm`);
                    const direction = mm < 0 ? 'left' : 'right';
                    moveAxis(direction, Math.abs(mm));
                });
            });
            
            // Botón home del jog card
            const btnHomeJog = document.getElementById('btnHome');
            if (btnHomeJog) {
                btnHomeJog.addEventListener('click', () => {
                    console.log('🎮 Homing desde jog card');
                    homeAxis();
                });
            }
            
            // Guardar configuración
            const saveAxisBtn = document.getElementById('saveAxis');
            if (saveAxisBtn) {
                saveAxisBtn.addEventListener('click', () => {
                    console.log('💾 Guardando configuración del eje');
                    saveAxisConfig();
                });
            }
            
            // Navegación por teclado
            document.addEventListener('keydown', (e) => {
                let step = 1;
                if (e.shiftKey) step = 10;
                
                if (e.key === 'ArrowLeft') {
                    console.log(`⌨️ Tecla ←: mover -${step}mm`);
                    moveAxis('left', step);
                    e.preventDefault();
                } else if (e.key === 'ArrowRight') {
                    console.log(`⌨️ Tecla →: mover +${step}mm`);
                    moveAxis('right', step);
                    e.preventDefault();
                }
            });
        }

        // Event listeners para botones de tara
        document.addEventListener('DOMContentLoaded', function() {
            // Obtener información del sistema al cargar
            getSystemInfo();
            
            // Configurar botón de tara
            const taraButton = document.getElementById('taraButton');
            if (taraButton) {
                taraButton.addEventListener('click', performTara);
            }
            
            // Configurar controles del eje
            setupAxisControls();
            
            // Actualizar pesos cada 2 segundos
            setInterval(updateWeights, 2000);
            
            // Actualizar información del sistema cada 30 segundos
            setInterval(getSystemInfo, 30000);
            
            // Inicializar gráfico
            initHistoryChart();
            
            // Inicializar primera actualización
            updateWeights();
        });

        // Función para copiar IP al portapapeles
        function copyIPToClipboard() {
            if (currentIP) {
                navigator.clipboard.writeText(currentIP).then(() => {
                    alert('IP copiada al portapapeles: ' + currentIP);
                });
            }
        }

        // Hacer la IP clickeable para copiar
        if (currentIpElement) {
            currentIpElement.style.cursor = 'pointer';
            currentIpElement.title = 'Haz click para copiar la IP';
            currentIpElement.addEventListener('click', copyIPToClipboard);
        }
    </script>
</body>
</html>
)=====";