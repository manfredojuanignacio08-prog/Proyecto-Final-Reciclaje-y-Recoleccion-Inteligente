const char PAGINA_PRESENTACION[] = 
R"=====(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>EcoSmart - Proyecto de Reciclaje Inteligente</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Poppins:wght@400;600;700;800&family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
    <style>
        /* === ESTILOS COMPLETOS DE LA PRESENTACIÓN === */
        :root {
            --primary: #2a9d8f;
            --primary-dark: #238a7e;
            --secondary: #264653;
            --accent: #e9c46a;
            --success: #06d6a0;
            --warning: #ffd166;
            --danger: #ef476f;
            
            --bg-main: #f8f9fa;
            --bg-alt: #ffffff;
            --text-primary: #1a202c;
            --text-secondary: #718096;
            --border: #e2e8f0;
            
            --shadow-sm: 0 2px 8px rgba(0,0,0,0.04);
            --shadow-md: 0 4px 16px rgba(0,0,0,0.08);
            --shadow-lg: 0 8px 32px rgba(0,0,0,0.12);
        }

        body.dark-mode {
            --primary: #10b981;
            --primary-dark: #059669;
            --secondary: #f3f4f6;
            --accent: #fbbf24;
            --success: #34d399;
            --warning: #fbbf24;
            --danger: #f87171;
            
            --bg-main: #0f172a;
            --bg-alt: #1e293b;
            --text-primary: #f1f5f9;
            --text-secondary: #94a3b8;
            --border: #334155;
            
            --shadow-sm: 0 2px 8px rgba(0,0,0,0.3);
            --shadow-md: 0 4px 16px rgba(0,0,0,0.4);
            --shadow-lg: 0 8px 32px rgba(0,0,0,0.5);
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        html {
            scroll-behavior: smooth;
        }

        body {
            font-family: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
            background: var(--bg-main);
            color: var(--text-primary);
            line-height: 1.6;
            transition: background-color 0.3s, color 0.3s;
        }

        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 0 2rem;
        }

        /* === HEADER === */
        .header {
            background: var(--bg-alt);
            border-bottom: 1px solid var(--border);
            position: sticky;
            top: 0;
            z-index: 1000;
            box-shadow: var(--shadow-sm);
            backdrop-filter: blur(10px);
        }

        .nav-main {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 1rem 2rem;
            max-width: 1400px;
            margin: 0 auto;
        }

        .logo-container {
            display: flex;
            align-items: center;
            gap: 1rem;
            text-decoration: none;
        }

        .logo {
            height: 48px;
            width: 48px;
            border-radius: 12px;
            box-shadow: 0 2px 8px rgba(42, 157, 143, 0.2);
            background: #2a9d8f;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-weight: bold;
            font-size: 1.2rem;
        }

        .project-name {
            display: block;
            font-family: 'Poppins', sans-serif;
            font-size: 1.4rem;
            font-weight: 800;
            background: linear-gradient(135deg, var(--primary), var(--success));
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
        }

        .project-tagline {
            display: block;
            font-size: 0.75rem;
            color: var(--text-secondary);
            font-weight: 600;
            margin-top: -2px;
        }

        .nav-links {
            display: flex;
            list-style: none;
            gap: 2rem;
        }

        .nav-links a {
            text-decoration: none;
            color: var(--text-primary);
            font-weight: 600;
            font-size: 0.95rem;
            transition: all 0.3s;
            position: relative;
        }

        .nav-links a::after {
            content: '';
            position: absolute;
            bottom: -4px;
            left: 0;
            width: 0;
            height: 2px;
            background: var(--primary);
            transition: width 0.3s;
        }

        .nav-links a:hover::after {
            width: 100%;
        }

        .nav-actions {
            display: flex;
            align-items: center;
            gap: 1rem;
        }

        .dashboard-button {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            padding: 0.6rem 1.2rem;
            background: linear-gradient(135deg, var(--primary), var(--primary-dark));
            color: white;
            text-decoration: none;
            border-radius: 50px;
            font-weight: 600;
            font-size: 0.9rem;
            transition: all 0.3s;
            box-shadow: 0 4px 12px rgba(42, 157, 143, 0.3);
        }

        .dashboard-button:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 20px rgba(42, 157, 143, 0.4);
        }

        .theme-button {
            width: 44px;
            height: 44px;
            border-radius: 50%;
            background: var(--bg-main);
            border: 1px solid var(--border);
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
            font-size: 1.3rem;
            transition: all 0.3s;
        }

        .theme-button:hover {
            transform: scale(1.1) rotate(15deg);
            border-color: var(--primary);
        }

        .moon-icon {
            display: none;
        }

        body.dark-mode .sun-icon {
            display: none;
        }

        body.dark-mode .moon-icon {
            display: block;
        }

        /* === HERO SECTION === */
        .hero {
            position: relative;
            min-height: 90vh;
            display: flex;
            align-items: center;
            justify-content: center;
            text-align: center;
            padding: 4rem 2rem;
            background: linear-gradient(135deg, #2a9d8f 0%, #264653 100%);
            overflow: hidden;
        }

        .hero::before {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: url('data:image/svg+xml,<svg width="60" height="60" xmlns="http://www.w3.org/2000/svg"><path d="M0 0h60v60H0z" fill="none"/><path d="M30 0v30M60 30H30" stroke="rgba(255,255,255,0.05)" stroke-width="1"/></svg>');
            opacity: 0.3;
        }

        .hero-overlay {
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: radial-gradient(circle at 50% 50%, transparent 0%, rgba(0,0,0,0.3) 100%);
        }

        .hero-content {
            position: relative;
            z-index: 1;
            color: white;
            max-width: 900px;
            margin: 0 auto;
        }

        .hero-badge {
            display: inline-block;
            padding: 0.5rem 1.5rem;
            background: rgba(255,255,255,0.15);
            border-radius: 50px;
            font-weight: 600;
            font-size: 0.9rem;
            margin-bottom: 2rem;
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255,255,255,0.2);
        }

        .hero-content h1 {
            font-family: 'Poppins', sans-serif;
            font-size: 4rem;
            font-weight: 800;
            margin-bottom: 1.5rem;
            line-height: 1.1;
        }

        .highlight {
            background: linear-gradient(135deg, #06d6a0, #ffd166);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
        }

        .subtitle {
            font-size: 1.3rem;
            opacity: 0.95;
            margin-bottom: 3rem;
            line-height: 1.6;
        }

        .hero-buttons {
            display: flex;
            gap: 1rem;
            justify-content: center;
            margin-bottom: 4rem;
        }

        .cta-button {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            padding: 1rem 2rem;
            border-radius: 50px;
            font-weight: 700;
            font-size: 1rem;
            text-decoration: none;
            transition: all 0.3s;
        }

        .cta-primary {
            background: white;
            color: var(--primary);
            box-shadow: 0 4px 20px rgba(0,0,0,0.2);
        }

        .cta-primary:hover {
            transform: translateY(-4px);
            box-shadow: 0 8px 30px rgba(0,0,0,0.3);
        }

        .cta-secondary {
            background: rgba(255,255,255,0.1);
            color: white;
            border: 2px solid rgba(255,255,255,0.3);
            backdrop-filter: blur(10px);
        }

        .cta-secondary:hover {
            background: rgba(255,255,255,0.2);
            transform: translateY(-4px);
        }

        .hero-stats {
            display: flex;
            gap: 3rem;
            justify-content: center;
        }

        .stat-item {
            text-align: center;
        }

        .stat-icon {
            font-size: 3rem;
            margin-bottom: 0.5rem;
        }

        .stat-value {
            font-family: 'Poppins', sans-serif;
            font-size: 2.5rem;
            font-weight: 800;
            display: block;
        }

        .stat-label {
            font-size: 0.9rem;
            opacity: 0.9;
            text-transform: uppercase;
            letter-spacing: 1px;
        }

        /* === SECTIONS === */
        .content-section {
            padding: 6rem 0;
        }

        .content-section.alt-bg {
            background: var(--bg-alt);
        }

        .section-header {
            text-align: center;
            margin-bottom: 4rem;
        }

        .section-badge {
            display: inline-block;
            padding: 0.5rem 1.5rem;
            background: var(--bg-alt);
            border: 2px solid var(--border);
            border-radius: 50px;
            font-weight: 700;
            font-size: 0.9rem;
            margin-bottom: 1.5rem;
            color: var(--primary);
        }

        .section-header h2 {
            font-family: 'Poppins', sans-serif;
            font-size: 3rem;
            font-weight: 800;
            margin-bottom: 1rem;
            color: var(--text-primary);
        }

        .section-intro {
            font-size: 1.2rem;
            color: var(--text-secondary);
            max-width: 700px;
            margin: 0 auto;
        }

        /* === PROBLEM SECTION === */
        .problem-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 2rem;
            margin-bottom: 4rem;
        }

        .problem-card {
            background: var(--bg-alt);
            padding: 2.5rem;
            border-radius: 20px;
            border: 1px solid var(--border);
            box-shadow: var(--shadow-md);
            transition: all 0.3s;
            text-align: center;
        }

        .problem-card:hover {
            transform: translateY(-8px);
            box-shadow: var(--shadow-lg);
        }

        .problem-icon {
            font-size: 4rem;
            margin-bottom: 1.5rem;
        }

        .problem-card h3 {
            font-family: 'Poppins', sans-serif;
            font-size: 1.4rem;
            margin-bottom: 1rem;
            color: var(--text-primary);
        }

        .problem-card p {
            color: var(--text-secondary);
            margin-bottom: 2rem;
            line-height: 1.6;
        }

        .problem-stat {
            font-family: 'Poppins', sans-serif;
            font-size: 3rem;
            font-weight: 800;
            color: var(--danger);
            margin-bottom: 0.5rem;
        }

        .problem-stat-label {
            font-size: 0.9rem;
            color: var(--text-secondary);
            text-transform: uppercase;
            letter-spacing: 1px;
        }

        /* === RESPONSIVE === */
        @media (max-width: 768px) {
            .nav-main {
                flex-direction: column;
                gap: 1rem;
            }
            
            .nav-links {
                flex-wrap: wrap;
                gap: 1rem;
                justify-content: center;
            }
            
            .hero-content h1 {
                font-size: 2.5rem;
            }
            
            .hero-stats {
                flex-direction: column;
                gap: 2rem;
            }
            
            .section-header h2 {
                font-size: 2rem;
            }
        }

        /* === EQUIPO SECTION === */
        .team-grid-compact {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 2rem;
            margin-bottom: 3rem;
        }

        .team-card {
            background: var(--bg-alt);
            padding: 2.5rem 2rem;
            border-radius: 20px;
            text-align: center;
            border: 1px solid var(--border);
            box-shadow: var(--shadow-md);
            transition: all 0.3s;
        }

        .team-card:hover {
            transform: translateY(-8px);
            box-shadow: var(--shadow-lg);
        }

        .team-avatar {
            font-size: 4rem;
            margin-bottom: 1rem;
        }

        .team-card h3 {
            font-family: 'Poppins', sans-serif;
            font-size: 1.1rem;
            margin-bottom: 0.5rem;
            color: var(--text-primary);
        }

        .team-role {
            color: var(--text-secondary);
            font-size: 0.9rem;
            font-weight: 600;
        }

        .team-skills {
            display: flex;
            flex-wrap: wrap;
            gap: 0.5rem;
            justify-content: center;
            margin-top: 1rem;
        }

        .skill-tag {
            padding: 0.4rem 0.8rem;
            background: var(--bg-main);
            border-radius: 50px;
            font-size: 0.8rem;
            font-weight: 600;
            color: var(--primary);
            border: 1px solid var(--border);
        }

        /* === FOOTER === */
        .footer {
            background: var(--bg-alt);
            border-top: 1px solid var(--border);
            padding: 3rem 0 1rem;
            margin-top: 4rem;
        }

        .footer-content {
            display: grid;
            grid-template-columns: 2fr 1fr 1fr;
            gap: 3rem;
            margin-bottom: 2rem;
        }

        .footer-logo {
            height: 40px;
            margin-bottom: 1rem;
            background: #2a9d8f;
            border-radius: 8px;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-weight: bold;
        }

        .footer-brand strong {
            font-family: 'Poppins', sans-serif;
            font-size: 1.2rem;
            color: var(--primary);
        }

        .footer-desc {
            color: var(--text-secondary);
            margin-top: 0.5rem;
        }

        .footer-links h4,
        .footer-contact h4 {
            font-family: 'Poppins', sans-serif;
            margin-bottom: 1rem;
            color: var(--text-primary);
        }

        .footer-links a {
            display: block;
            color: var(--text-secondary);
            text-decoration: none;
            margin-bottom: 0.5rem;
            transition: all 0.3s;
        }

        .footer-links a:hover {
            color: var(--primary);
            transform: translateX(4px);
        }

        .footer-contact p {
            color: var(--text-secondary);
            margin-bottom: 0.5rem;
        }

        .footer-bottom {
            text-align: center;
            padding-top: 2rem;
            border-top: 1px solid var(--border);
            color: var(--text-secondary);
        }
    </style>
</head>
<body>

    <header class="header">
        <nav class="nav-main">
            <a href="#inicio" class="logo-container">
                <div class="logo">🌱</div>
                <div>
                    <span class="project-name">EcoSmart</span>
                    <span class="project-tagline">♻️ Reciclaje Inteligente</span>
                </div>
            </a>
            <ul class="nav-links">
                <li><a href="#problema">🚨 El Problema</a></li>
                <li><a href="#solucion">💡 La Solución</a></li>
                <li><a href="#equipo">👥 Equipo</a></li>
                <li><a href="#contacto">📞 Contacto</a></li>
            </ul>
            <div class="nav-actions">
                <a href="/dashboard" class="dashboard-button">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                        <rect x="3" y="3" width="7" height="7"></rect>
                        <rect x="14" y="3" width="7" height="7"></rect>
                        <rect x="14" y="14" width="7" height="7"></rect>
                        <rect x="3" y="14" width="7" height="7"></rect>
                    </svg>
                    Dashboard
                </a>
                <button id="theme-toggle" class="theme-button">
                    <span class="sun-icon">☀️</span>
                    <span class="moon-icon">🌙</span>
                </button>
            </div>
        </nav>
    </header>

    <main>
        <!-- Hero Section -->
        <section id="inicio" class="hero">
            <div class="hero-overlay"></div>
            <div class="hero-content">
                <div class="hero-badge">🌍 Proyecto Innovador 2024</div>
                <h1>Proyecto de Reciclaje <span class="highlight">Inteligente</span></h1>
                <p class="subtitle">Una solución innovadora que utiliza tecnología para automatizar la clasificación de residuos y fomentar un futuro más sostenible</p>
                <div class="hero-buttons">
                    <a href="#problema" class="cta-button cta-primary">
                        <span>Descubre el Proyecto</span>
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                            <line x1="5" y1="12" x2="19" y2="12"></line>
                            <polyline points="12 5 19 12 12 19"></polyline>
                        </svg>
                    </a>
                    <a href="/dashboard" class="cta-button cta-secondary">
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                            <polygon points="5 3 19 12 5 21 5 3"></polygon>
                        </svg>
                        <span>Ver Demo</span>
                    </a>
                </div>
                <div class="hero-stats">
                    <div class="stat-item">
                        <div class="stat-icon">♻️</div>
                        <div class="stat-value">3</div>
                        <div class="stat-label">Contenedores</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-icon">🤖</div>
                        <div class="stat-value">100%</div>
                        <div class="stat-label">Automatizado</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-icon">📊</div>
                        <div class="stat-value">24/7</div>
                        <div class="stat-label">Monitoreo</div>
                    </div>
                </div>
            </div>
        </section>

        <!-- Problema Section -->
        <section id="problema" class="content-section">
            <div class="container">
                <div class="section-header">
                    <span class="section-badge">🚨 Desafío Global</span>
                    <h2>El Problema del Reciclaje Ineficiente</h2>
                    <p class="section-intro">La gestión de residuos es uno de los mayores desafíos ambientales. La mezcla de materiales contamina grandes volúmenes de desechos.</p>
                </div>
                
                <div class="problem-grid">
                    <div class="problem-card">
                        <div class="problem-icon">⚠️</div>
                        <h3>Contaminación Cruzada</h3>
                        <p>Un solo residuo mal clasificado puede arruinar un lote entero de material reciclable</p>
                        <div class="problem-stat">85%</div>
                        <div class="problem-stat-label">de residuos mal clasificados</div>
                    </div>
                    
                    <div class="problem-card">
                        <div class="problem-icon">📉</div>
                        <h3>Baja Tasa de Reciclaje</h3>
                        <p>La falta de separación adecuada es la principal causa de las bajas tasas de reciclaje mundial</p>
                        <div class="problem-stat">15%</div>
                        <div class="problem-stat-label">tasa de reciclaje actual</div>
                    </div>
                    
                    <div class="problem-card">
                        <div class="problem-icon">💰</div>
                        <h3>Costos Elevados</h3>
                        <p>La clasificación manual es lenta, costosa y expone a trabajadores a condiciones insalubres</p>
                        <div class="problem-stat">+40%</div>
                        <div class="problem-stat-label">sobrecosto operativo</div>
                    </div>
                </div>
            </div>
        </section>

        <!-- Solución Section -->
        <section id="solucion" class="content-section alt-bg">
            <div class="container">
                <div class="section-header">
                    <span class="section-badge">💡 Innovación</span>
                    <h2>Nuestra Solución: EcoSmart</h2>
                    <p class="section-intro">Un sistema automatizado que identifica y separa residuos utilizando sensores inteligentes</p>
                </div>

                <div class="problem-grid">
                    <div class="problem-card">
                        <div class="problem-icon">🎯</div>
                        <h3>Clasificación Automática</h3>
                        <p>Sistema inteligente que detecta y separa metales, orgánicos y resto automáticamente</p>
                    </div>
                    
                    <div class="problem-card">
                        <div class="problem-icon">⚖️</div>
                        <h3>Monitoreo en Tiempo Real</h3>
                        <p>Sensores de peso que monitorean el nivel de llenado de cada contenedor</p>
                    </div>
                    
                    <div class="problem-card">
                        <div class="problem-icon">📊</div>
                        <h3>Dashboard Inteligente</h3>
                        <p>Interfaz web para monitoreo remoto y gestión de datos del sistema</p>
                    </div>
                </div>
            </div>
        </section>

        <!-- Equipo Section -->
        <section id="equipo" class="content-section">
            <div class="container">
                <div class="section-header">
                    <span class="section-badge">👥 Nuestro Equipo</span>
                    <h2>Quiénes Somos</h2>
                    <p class="section-intro">Estudiantes de 6° año de Informática B del Instituto Leonardo Murialdo</p>
                </div>
                
                <div class="team-grid-compact">
                    <div class="team-card">
                        <div class="team-avatar">👨‍💻</div>
                        <h3>Manfredo Juan Ignacio</h3>
                        <p class="team-role">💻 Desarrollo Full-Stack</p>
                        <div class="team-skills">
                            <span class="skill-tag">API Flask</span>
                            <span class="skill-tag">Dashboard</span>
                            <span class="skill-tag">Hardware</span>
                        </div>
                    
                        <div class="team-avatar">👨‍💻</div>
                        <h3>Ferrando Jaco</h3>
                        <p class="team-role">🔌 Backend & IoT</p>
                        <div class="team-skills">
                            <span class="skill-tag">Python</span>
                            <span class="skill-tag">SQLite</span>
                            <span class="skill-tag">ESP32</span>
                        </div>
                  
                        <div class="team-avatar">👩‍💻</div>
                        <h3>Iannone Mia</h3>
                        <p class="team-role">🎨 Frontend & UX</p>
                        <div class="team-skills">
                            <span class="skill-tag">HTML/CSS</span>
                            <span class="skill-tag">JavaScript</span>
                            <span class="skill-tag">PWA</span>
                        </div>
                    </div>
                    <div class="team-card">
                        <div class="team-avatar">👨‍💻</div>
                        <h3>Siciliano Thiago</h3>
                        <p class="team-role">🧪 Testing & QA</p>
                        <div class="team-skills">
                            <span class="skill-tag">Validación</span>
                            <span class="skill-tag">Calibración</span>
                            <span class="skill-tag">Pruebas</span>
                        </div>
                    
                        <div class="team-avatar">👨‍💻</div>
                        <h3>Montenegro Franco</h3>
                        <p class="team-role">🔧 Electrónica</p>
                        <div class="team-skills">
                            <span class="skill-tag">Sensores</span>
                            <span class="skill-tag">Circuitos</span>
                            <span class="skill-tag">Actuadores</span>
                        </div>
                    </div>
                </div>
            </div>
        </section>

        <!-- Contacto Section -->
        <section id="contacto" class="content-section alt-bg">
            <div class="container">
                <div class="section-header">
                    <span class="section-badge">📞 Contacto</span>
                    <h2>¿Tienes Consultas sobre EcoSmart?</h2>
                    <p class="section-intro">Contáctanos para más información sobre el proyecto</p>
                </div>
                
                <div style="text-align: center; padding: 2rem;">
                    <div style="font-size: 1.2rem; margin-bottom: 2rem;">
                        <p>📧 <strong>ecosmart@gmail.com</strong></p>
                        <p>🏫 <strong>Instituto Leonardo Murialdo</strong></p>
                        <p>📍 <strong>Buenos Aires, Argentina</strong></p>
                    </div>
                    <a href="/" class="dashboard-button" style="font-size: 1.1rem; padding: 1rem 2rem;">
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                            <rect x="3" y="3" width="7" height="7"></rect>
                            <rect x="14" y="3" width="7" height="7"></rect>
                            <rect x="14" y="14" width="7" height="7"></rect>
                            <rect x="3" y="14" width="7" height="7"></rect>
                        </svg>
                        Ir al Dashboard
                    </a>
                </div>
            </div>
        </section>
    </main>

    <footer class="footer">
        <div class="container">
            <div class="footer-content">
                <div class="footer-brand">
                    <div class="footer-logo">🌱</div>
                    <p><strong>EcoSmart</strong> - Sistema de Reciclaje Inteligente</p>
                    <p class="footer-desc">Proyecto Final 2024 • Instituto Leonardo Murialdo</p>
                </div>
                <div class="footer-links">
                    <h4>Enlaces</h4>
                    <a href="#inicio">Inicio</a>
                    <a href="#problema">El Problema</a>
                    <a href="#solucion">Solución</a>
                    <a href="/">Dashboard</a>
                </div>
                <div class="footer-contact">
                    <h4>Contacto</h4>
                    <p>📧 ecosmart@gmail.com</p>
                    <p>📍 Buenos Aires, Argentina</p>
                </div>
            </div>
            <div class="footer-bottom">
                <p>&copy; 2024 EcoSmart • Todos los derechos reservados</p>
            </div>
        </div>
    </footer>

    <script>
        // Toggle tema oscuro
        const themeToggle = document.getElementById('theme-toggle');
        const body = document.body;
        
        const applyTheme = (theme) => {
            if (theme === 'dark-mode') {
                body.classList.add('dark-mode');
                localStorage.setItem('theme', 'dark-mode');
            } else {
                body.classList.remove('dark-mode');
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

        // Smooth scroll para enlaces internos
        document.querySelectorAll('a[href^="#"]').forEach(anchor => {
            anchor.addEventListener('click', function (e) {
                e.preventDefault();
                const target = document.querySelector(this.getAttribute('href'));
                if (target) {
                    target.scrollIntoView({
                        behavior: 'smooth',
                        block: 'start'
                    });
                }
            });
        });
    </script>
</body>
</html>
)=====";