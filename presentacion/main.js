document.addEventListener('DOMContentLoaded', function() {

    // --- LÓGICA DEL TEMA OSCURO ---
    const themeToggle = document.getElementById('theme-toggle');
    const currentTheme = localStorage.getItem('theme');

    // Aplicar tema guardado al cargar la página
    if (currentTheme) {
        document.body.classList.add(currentTheme);
        if (currentTheme === 'dark-mode') {
            themeToggle.textContent = '🌙';
        }
    }

    // Event listener para el botón
    themeToggle.addEventListener('click', () => {
        document.body.classList.toggle('dark-mode');
        let theme = 'light-mode';
        if (document.body.classList.contains('dark-mode')) {
            theme = 'dark-mode';
            themeToggle.textContent = '🌙';
        } else {
            themeToggle.textContent = '☀️';
        }
        localStorage.setItem('theme', theme);
    });

    // --- Carga de Tablas desde archivos CSV ---
    async function loadTable(csvPath, tableId) {
        try {
            const response = await fetch(csvPath);
            if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
            const csvText = await response.text();
            const table = document.getElementById(tableId);
            if (!table) return;

            table.innerHTML = ''; 
            const rows = csvText.trim().split('\n');
            
            const header = table.createTHead().insertRow();
            rows[0].split(',').forEach(text => {
                const th = document.createElement('th');
                th.textContent = text.trim();
                header.appendChild(th);
            });

            const body = table.createTBody();
            for (let i = 1; i < rows.length; i++) {
                const row = body.insertRow();
                rows[i].split(',').forEach(text => {
                    const cell = row.insertCell();
                    cell.textContent = text.trim();
                });
            }
        } catch (error) {
            console.error('Error al cargar la tabla:', error);
            const table = document.getElementById(tableId);
            if (table) table.innerHTML = '<tr><td>Error al cargar los datos.</td></tr>';
        }
    }

    loadTable('Tabla de Precios - Manfredo, Ferrando, Iannone, Siciliano y Montenegro 3 (1) (1).xlsx - Precios Componentes.csv', 'tabla-componentes');
    loadTable('Tabla de Precios - Manfredo, Ferrando, Iannone, Siciliano y Montenegro 3 (1) (1).xlsx - Precios Materiales No Hardware.csv', 'tabla-materiales');
    loadTable('Tabla de Precios - Manfredo, Ferrando, Iannone, Siciliano y Montenegro 3 (1) (1).xlsx - Precios Total.csv', 'tabla-total');

    // --- Navegación suave ---
    document.querySelectorAll('.nav-links a, .cta-button').forEach(link => {
        link.addEventListener('click', function(e) {
            const href = this.getAttribute('href');
            if (href.startsWith('#')) {
                e.preventDefault();
                document.querySelector(href).scrollIntoView({ behavior: 'smooth' });
            }
        });
    });
});