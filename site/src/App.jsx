import { useEffect, useState } from 'react'

const REPO = 'flessan/memreduct-linux'
const GH = `https://api.github.com/repos/${REPO}`
const INSTALL_CMD = `curl -fsSL https://slate.dotfiles.qzz.io/r.sh | bash`

/* ------------------------------------------------------------------ */
/* Doodle-style Icons (Thick strokes, rounded joins)                  */
/* ------------------------------------------------------------------ */
const IconCopy = () => <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2" /><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1" /></svg>
const IconCheck = () => <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><polyline points="20 6 9 17 4 12" /></svg>
const IconStar = () => <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor" stroke="none"><polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2" /></svg>
const IconFork = () => <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="18" r="3" /><circle cx="6" cy="6" r="3" /><circle cx="18" cy="6" r="3" /><path d="M6 21V9a6 6 0 0 0 6 6h0a6 6 0 0 0 6-6v12" /></svg>
const IconAlert = () => <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="10" /><line x1="12" y1="8" x2="12" y2="12" /><line x1="12" y1="16" x2="12.01" y2="16" /></svg>
const IconGithub = () => <svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor"><path d="M12 0c-6.626 0-12 5.373-12 12 0 5.302 3.438 9.8 8.207 11.387.599.111.793-.261.793-.577v-2.234c-3.338.726-4.033-1.416-4.033-1.416-.546-1.387-1.333-1.756-1.333-1.756-1.089-.745.083-.729.083-.729 1.205.084 1.839 1.237 1.839 1.237 1.07 1.834 2.807 1.304 3.492.997.107-.775.418-1.305.762-1.604-2.665-.305-5.467-1.334-5.467-5.931 0-1.311.469-2.381 1.236-3.221-.124-.303-.535-1.524.117-3.176 0 0 1.008-.322 3.301 1.23.957-.266 1.983-.399 3.003-.404 1.02.005 2.047.138 3.006.404 2.291-1.552 3.297-1.23 3.297-1.23.653 1.653.242 2.874.118 3.176.77.84 1.235 1.911 1.235 3.221 0 4.609-2.807 5.624-5.479 5.921.43.372.823 1.102.823 2.222v3.293c0 .319.192.694.801.576 4.765-1.589 8.199-6.086 8.199-11.386 0-6.627-5.373-12-12-12z" /></svg>

const IconMonitor = () => <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><rect x="2" y="3" width="20" height="14" rx="2" /><path d="M8 21h8" /><path d="M12 17v4" /></svg>
const IconSparkles = () => <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="m12 3-1.912 5.813a2 2 0 0 1-1.275 1.275L3 12l5.813 1.912a2 2 0 0 1 1.275 1.275L12 21l1.912-5.813a2 2 0 0 1 1.275-1.275L21 12l-5.813-1.912a2 2 0 0 1-1.275-1.275L12 3Z" /></svg>
const IconSettings = () => <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z" /><circle cx="12" cy="12" r="3" /></svg>
const IconTerminal = () => <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><polyline points="4 17 10 11 4 5" /><line x1="12" y1="19" x2="20" y2="19" /></svg>
const IconFeather = () => <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M20.24 12.24a6 6 0 0 0-8.49-8.49L5 10.5V19h8.5z" /><line x1="16" y1="8" x2="2" y2="22" /><line x1="17.5" y1="15" x2="9" y2="15" /></svg>
const IconCode = () => <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><polyline points="16 18 22 12 16 6" /><polyline points="8 6 2 12 8 18" /></svg>
const IconShield = () => <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z" /></svg>
const IconPackage = () => <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.5" strokeLinecap="round" strokeLinejoin="round"><path d="M16.5 9.4 7.55 4.24" /><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z" /><polyline points="3.29 7 12 12 20.71 7" /><line x1="12" y1="22" x2="12" y2="12" /></svg>

/* ------------------------------------------------------------------ */
/* GitHub API hook                                                    */
/* ------------------------------------------------------------------ */
function useGitHub(path) {
  const [data, setData] = useState(null)
  useEffect(() => {
    const key = `gh:${path}`
    const cached = localStorage.getItem(key)
    if (cached) {
      try {
        const { time, value } = JSON.parse(cached)
        if (Date.now() - time < 10 * 60 * 1000) {
          setData(value)
          return
        }
      } catch { }
    }
    fetch(`${GH}${path}`)
      .then((r) => (r.ok ? r.json() : Promise.reject(r.status)))
      .then((value) => {
        setData(value)
        localStorage.setItem(key, JSON.stringify({ time: Date.now(), value }))
      })
      .catch(() => setData(null))
  }, [path])
  return data
}

const fmt = (n) => (n == null ? '-' : n >= 1000 ? `${(n / 1000).toFixed(1)}k` : String(n))

/* ------------------------------------------------------------------ */
/* Building blocks                                                    */
/* ------------------------------------------------------------------ */
function CopyButton({ text }) {
  const [copied, setCopied] = useState(false)
  const copy = () => {
    navigator.clipboard.writeText(text).then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 1600)
    })
  }
  return (
    <button className={`copy-btn ${copied ? 'copied' : ''}`} onClick={copy} aria-label="Copy to clipboard">
      {copied ? <IconCheck /> : <IconCopy />}
      <span>{copied ? 'Copied!' : ''}</span>
    </button>
  )
}

function CommandBox({ cmd, prompt = '$' }) {
  return (
    <div className="cmdbox">
      <code>
        <span className="prompt">{prompt}</span> {cmd}
      </code>
      <CopyButton text={cmd} />
    </div>
  )
}

function Bar({ pct, colorVar = '--accent' }) {
  const width = 26
  const filled = Math.round((pct / 100) * width)
  return (
    <span className="terminal-bar-visual">
      <span style={{ color: `var(${colorVar})` }}>{'█'.repeat(filled)}</span>
      <span style={{ color: 'var(--terminal-dim)' }}>{'░'.repeat(width - filled)}</span>
    </span>
  )
}

/* ------------------------------------------------------------------ */
/* Terminal demo                                                      */
/* ------------------------------------------------------------------ */
function TerminalDemo() {
  const [tab, setTab] = useState('status')
  const tabs = ['status', 'top', 'clean', 'monitor']

  return (
    <div className="terminal">
      <div className="terminal-bar">
        <div className="terminal-controls">
          <span className="dot red" />
          <span className="dot yellow" />
          <span className="dot green" />
        </div>
        <span className="terminal-title">memreduct - zsh - 80x24</span>
      </div>
      <div className="terminal-tabs">
        {tabs.map((t) => (
          <button key={t} className={tab === t ? 'active' : ''} onClick={() => setTab(t)}>
            {t}
          </button>
        ))}
      </div>
      <pre className="terminal-body">
        {tab === 'status' && (
          <>
            <span className="prompt">$</span> memreduct<span className="cursor">▋</span>
            {'\n\n'}
            <span className="terminal-header">Mem Reduct 1.1.0</span>{'\n\n'}
            Physical memory  <Bar pct={42} colorVar="--success" /> <span className="text-success">42.3%</span>  6.6 GB / 15.6 GB{'\n'}
            Cache            <Bar pct={31} colorVar="--accent" /> 31.0%  4.8 GB{'\n'}
            Swap             <Bar pct={4} colorVar="--success" /> <span className="text-success"> 4.1%</span>  334 MB / 8.0 GB{'\n'}
            Pressure         some <span className="text-success">0.12%</span>  full 0.00%  <span className="dim">(10s avg, PSI)</span>
          </>
        )}
        {tab === 'top' && (
          <>
            <span className="prompt">$</span> memreduct top -l 5<span className="cursor">▋</span>
            {'\n\n'}
            <span className="terminal-header">  PID        RSS   MEM%  NAME</span>{'\n'}
            {'  2841     1.9 GB  12.4%  firefox\n'}
            {'  3102   904.2 MB   5.8%  Isolated Web Co\n'}
            {'  1290   512.7 MB   3.3%  gnome-shell\n'}
            {'  4551   377.1 MB   2.4%  code\n'}
            {'  2093   201.5 MB   1.3%  spotify'}
          </>
        )}
        {tab === 'clean' && (
          <>
            <span className="prompt">$</span> sudo memreduct clean<span className="cursor">▋</span>
            {'\n\n'}
            Cleaning memory (page cache, dentries/inodes, compaction)...{'\n'}
            <span className="text-success">
              ✓ Memory cleaned: 1.2 GB reclaimed, cache reduced by 3.4 GB (61.8% -&lt; 47.1%)
            </span>
          </>
        )}
        {tab === 'monitor' && (
          <>
            <span className="prompt">$</span> sudo memreduct monitor<span className="cursor">▋</span>
            {'\n\n'}
            Physical memory  <Bar pct={61} colorVar="--warning" /> <span className="text-warning">61.8%</span>  9.6 GB / 15.6 GB{'\n'}
            History          <span className="text-success">▁▂▂▃▃▄▄▅</span><span className="text-warning">▅▆▆▇▆▅</span><span className="text-success">▄▃▂▂▁</span>{'\n\n'}
            <span className="terminal-header">Top processes</span>{'\n'}
            {'  2841     1.9 GB  12.4%  firefox\n'}
            {'  4551   377.1 MB   2.4%  code\n\n'}
            <span className="dim">[c] clean memory   [q] quit</span>
          </>
        )}
      </pre>
    </div>
  )
}

/* ------------------------------------------------------------------ */
/* Sections                                                           */
/* ------------------------------------------------------------------ */
function Hero({ repo, release }) {
  const version = release?.tag_name || 'v1.1.0'
  return (
    <header className="hero">
      <nav className="glass-nav">
        <div className="logo">
          <span className="logo-mark"><img src="./src/100.ico" width="20" style={{ alignItems: "center", marginBottom: "-4px" }} /></span> Mem Reduct <span className="for-linux">for Linux</span>
        </div>
        <div className="nav-links">
          <a href="#features">Features</a>
          <a href="#install">Install</a>
          <a href={`https://github.com/${REPO}`} className="gh-btn">
            <IconGithub /> {fmt(repo?.stargazers_count)}
          </a>
        </div>
      </nav>

      <div className="hero-inner">
        <div className="hero-text">
          <span className="version-pill">{version} · GPL-3.0</span>
          <h1>
            Monitor & clean your <span className="accent">memory</span>.<br />
            On every Linux distro.
          </h1>
          <p>
            Lightweight real-time memory management. The classic Windows tool,
            rebuilt for Linux in ~1000 lines of dependency-free C. Drop caches,
            compact memory, auto-clean on a threshold. Nothing else.
          </p>
          <CommandBox cmd={INSTALL_CMD} />
          <p className="safety">
            <IconAlert /> It's safe, but if you don't trust one-liners blindly,{' '}
            <a href={`https://github.com/${REPO}/blob/master/install.sh`}>read the script</a>.
            It builds from source and runs <code>make install</code>.
          </p>
          <div className="hero-stats">
            <span><IconStar /> {fmt(repo?.stargazers_count)} stars</span>
            <span><IconFork /> {fmt(repo?.forks_count)} forks</span>
            <span>◉ {fmt(repo?.open_issues_count)} issues</span>
            <span>⬇ zero dependencies</span>
          </div>
        </div>
        <div className="hero-visual">
          <TerminalDemo />
        </div>
      </div>
    </header>
  )
}

const FEATURES = [
  [<IconMonitor />, 'Real-time monitoring', 'Colored usage bars, PSI memory pressure, live history graph and top memory consumers right in your terminal.'],
  [<IconSparkles />, 'One-shot cleaning', 'Drops clean page cache and dentry/inode slabs, compacts fragmented memory, optionally flushes swap back to RAM.'],
  [<IconSettings />, 'Auto-clean daemon', 'Cleans at a usage threshold and/or on a timer, with cooldown, systemd unit, journal logging and desktop notifications.'],
  [<IconTerminal />, 'Every distro', 'Plain C99 + libc + /proc. Debian, Fedora, Arch, Alpine, Void, Gentoo, NixOS - glibc or musl, x86 to RISC-V.'],
  [<IconFeather />, 'Actually lightweight', 'A single ~36 KB binary. No GUI toolkit, no runtime, no background bloat. `make static` gives one portable file.'],
  [<IconCode />, 'Script-friendly', 'JSON output for status and top, exit codes that behave, bash & fish completions, and a proper man page.'],
  [<IconShield />, 'Safe by design', 'Only official kernel interfaces: drop_caches, compact_memory, sync(). Non-destructive - clean pages only.'],
  [<IconPackage />, 'Native packaging', '.deb builder, RPM spec, Arch PKGBUILD, systemd service - or just `make install` anywhere.'],
]

function Features() {
  return (
    <section id="features" className="section">
      <h2>Everything you need.<br /><span className="accent">Nothing you don't.</span></h2>
      <div className="grid">
        {FEATURES.map(([icon, title, body]) => (
          <div className="card" key={title}>
            <div className="card-icon">{icon}</div>
            <h3>{title}</h3>
            <p>{body}</p>
          </div>
        ))}
      </div>
    </section>
  )
}

function Install() {
  const [method, setMethod] = useState('quick')
  const methods = {
    quick: { label: 'Quick (any distro)', cmds: [INSTALL_CMD] },
    debian: { label: 'Debian / Ubuntu', cmds: [`git clone https://github.com/${REPO} && cd memreduct-linux`, './packaging/debian/build-deb.sh && sudo dpkg -i memreduct_*.deb'] },
    fedora: { label: 'Fedora / RHEL', cmds: [`git clone https://github.com/${REPO} && cd memreduct-linux`, 'rpmbuild -ba packaging/rpm/memreduct.spec'] },
    arch: { label: 'Arch', cmds: [`git clone https://github.com/${REPO} && cd memreduct-linux`, 'cd packaging/arch && makepkg -si'] },
    manual: { label: 'Manual', cmds: [`git clone https://github.com/${REPO} && cd memreduct-linux`, 'make && sudo make install'] },
  }

  return (
    <section id="install" className="section">
      <h2>Install in seconds</h2>
      <p className="section-sub">
        The quick installer opens a tiny menu: install, enable the auto-clean daemon,
        build a portable static binary, or uninstall. Works from bash, zsh, and fish.
      </p>
      <div className="install-tabs">
        {Object.entries(methods).map(([k, m]) => (
          <button key={k} className={method === k ? 'active' : ''} onClick={() => setMethod(k)}>
            {m.label}
          </button>
        ))}
      </div>
      <div className="install-cmds">
        {methods[method].cmds.map((c) => (
          <CommandBox key={c} cmd={c} />
        ))}
      </div>
      <p className="safety center">
        Everything builds from the source in this repository. Inspect{' '}
        <a href={`https://github.com/${REPO}/blob/master/install.sh`}>install.sh</a> and{' '}
        <a href={`https://github.com/${REPO}/blob/master/src/memreduct.c`}>memreduct.c</a> anytime.
        Uninstall with <code>… | bash -s -- --uninstall</code>.
      </p>
    </section>
  )
}

function GitHubSection({ repo, release, contributors }) {
  return (
    <section id="github" className="section">
      <h2>Open source, on GitHub</h2>
      <div className="gh-cards">
        <a className="gh-card" href={`https://github.com/${REPO}`}>
          <h3><IconGithub /> {REPO}</h3>
          <p>{repo?.description || 'Lightweight real-time memory management application to monitor and clean system memory.'}</p>
          <div className="gh-meta">
            <span><IconStar /> {fmt(repo?.stargazers_count)}</span>
            <span><IconFork /> {fmt(repo?.forks_count)}</span>
            <span>◉ {fmt(repo?.open_issues_count)} issues</span>
            <span>{repo?.license?.spdx_id || 'GPL-3.0'}</span>
            <span className="lang"><i /> C</span>
          </div>
          {repo?.pushed_at && (
            <div className="gh-updated">last commit {new Date(repo.pushed_at).toLocaleDateString()}</div>
          )}
        </a>

        <div className="gh-card">
          <h3>⭳ Latest release</h3>
          {release ? (
            <>
              <p><strong>{release.name || release.tag_name}</strong> · {new Date(release.published_at).toLocaleDateString()}</p>
              {(release.assets || []).slice(0, 4).map((a) => (
                <a key={a.id} className="asset" href={a.browser_download_url}>
                  {a.name} <span>{(a.size / 1024).toFixed(0)} KB · {fmt(a.download_count)}⬇</span>
                </a>
              ))}
              <a className="asset link-only" href={release.html_url}>release notes →</a>
            </>
          ) : (
            <>
              <p>No packaged release yet. Install straight from source with the one-liner; it always builds the latest code.</p>
              <a className="asset link-only" href={`https://github.com/${REPO}/releases`}>watch releases →</a>
            </>
          )}
        </div>

        <div className="gh-card">
          <h3>☻ Contributors</h3>
          <div className="avatars">
            {(contributors || []).slice(0, 12).map((c) => (
              <a key={c.id} href={c.html_url} title={`${c.login} · ${c.contributions} commits`}>
                <img src={`${c.avatar_url}&s=80`} alt={c.login} loading="lazy" />
              </a>
            ))}
            {!contributors && <p className="dim">loading…</p>}
          </div>
          <p className="credit">
            Based on <a href="https://github.com/henrypp/memreduct">Mem Reduct</a> by Henry++ - thank you!
          </p>
        </div>
      </div>
    </section>
  )
}

function Footer() {
  return (
    <footer>
      <p>
        <strong>Mem Reduct for Linux</strong> · GPL-3.0 ·{' '}
        <a href={`https://github.com/${REPO}`}>source</a> ·{' '}
        <a href={`https://github.com/${REPO}/issues`}>report a bug</a> ·
        original Windows version © 2011–2026 <a href="https://github.com/henrypp">Henry++</a>
      </p>
      <p className="dim">Live data on this page comes straight from the GitHub API.</p>
    </footer>
  )
}

/* ------------------------------------------------------------------ */
export default function App() {
  const repo = useGitHub('')
  const release = useGitHub('/releases/latest')
  const contributors = useGitHub('/contributors?per_page=12')

  const releaseOk = release && !release.message ? release : null
  const contributorsOk = Array.isArray(contributors) ? contributors : null

  return (
    <>
      <Hero repo={repo} release={releaseOk} />
      <Features />
      <Install />
      <GitHubSection repo={repo} release={releaseOk} contributors={contributorsOk} />
      <Footer />
    </>
  )
}