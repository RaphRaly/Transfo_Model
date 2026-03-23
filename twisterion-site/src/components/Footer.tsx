const NAV_LINKS = [
  { label: "Features", href: "#features" },
  { label: "Technology", href: "#technology" },
  { label: "Specs", href: "#specs" },
  { label: "Audio", href: "#audio" },
] as const;

const CONNECT_LINKS = [
  { label: "LinkedIn", href: "https://linkedin.com", external: true },
  { label: "GitHub", href: "https://github.com", external: true },
] as const;

export default function Footer() {
  return (
    <footer className="bg-bg-surface border-t border-border-subtle">
      <div className="max-w-7xl mx-auto px-6" style={{ paddingTop: '6rem', paddingBottom: '6rem' }}>
        {/* Three-column layout */}
        <div className="grid grid-cols-1 md:grid-cols-3 gap-12 mb-14">
          {/* Column 1: Brand */}
          <div>
            <span className="text-lg font-bold tracking-wider text-text-primary">
              TWISTERION
            </span>
            <p className="text-sm text-text-dim mt-2">
              Powered by HysteriCore
            </p>
            <p className="text-xs text-text-dim mt-4">
              &copy; 2026 TWISTERION
            </p>
          </div>

          {/* Column 2: Navigate */}
          <div>
            <h4 className="text-xs uppercase tracking-widest text-text-dim mb-3">
              Navigate
            </h4>
            <ul className="flex flex-col gap-2">
              {NAV_LINKS.map((link) => (
                <li key={link.href}>
                  <a
                    href={link.href}
                    className="text-sm text-text-muted hover:text-text-primary transition-colors duration-200"
                  >
                    {link.label}
                  </a>
                </li>
              ))}
            </ul>
          </div>

          {/* Column 3: Connect */}
          <div>
            <h4 className="text-xs uppercase tracking-widest text-text-dim mb-3">
              Connect
            </h4>
            <ul className="flex flex-col gap-2">
              {CONNECT_LINKS.map((link) => (
                <li key={link.label}>
                  <a
                    href={link.href}
                    target="_blank"
                    rel="noopener noreferrer"
                    className="text-sm text-text-muted hover:text-accent-cyan transition-colors duration-200"
                  >
                    {link.label}
                  </a>
                </li>
              ))}
            </ul>
          </div>
        </div>

        {/* Gradient divider line */}
        <div
          className="h-px w-full mb-6 rounded-full opacity-60"
          style={{
            background: "linear-gradient(to right, #06B6D4, #A855F7)",
          }}
        />

        {/* Tagline */}
        <p className="text-xs text-text-dim italic text-center">
          Built with physics, not shortcuts.
        </p>
      </div>
    </footer>
  );
}
