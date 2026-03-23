"use client";

import { useState, useEffect, useCallback } from "react";

const NAV_LINKS = [
  { label: "FEATURES", href: "#features" },
  { label: "TECHNOLOGY", href: "#technology" },
  { label: "SPECS", href: "#specs" },
  { label: "AUDIO", href: "#audio" },
] as const;

export default function Navbar() {
  const [scrolled, setScrolled] = useState(false);
  const [mobileOpen, setMobileOpen] = useState(false);

  useEffect(() => {
    const handleScroll = () => {
      setScrolled(window.scrollY > 32);
    };
    handleScroll();
    window.addEventListener("scroll", handleScroll, { passive: true });
    return () => window.removeEventListener("scroll", handleScroll);
  }, []);

  // Lock body scroll when mobile menu is open
  useEffect(() => {
    if (mobileOpen) {
      document.body.style.overflow = "hidden";
    } else {
      document.body.style.overflow = "";
    }
    return () => {
      document.body.style.overflow = "";
    };
  }, [mobileOpen]);

  const handleNavClick = useCallback(
    (e: React.MouseEvent<HTMLAnchorElement>, href: string) => {
      e.preventDefault();
      setMobileOpen(false);
      const target = document.querySelector(href);
      if (target) {
        target.scrollIntoView({ behavior: "smooth" });
      }
    },
    []
  );

  return (
    <>
      <nav
        className={`fixed top-0 left-0 right-0 z-50 h-16 flex items-center transition-all duration-300 ${
          scrolled
            ? "bg-bg-primary/90 backdrop-blur-xl border-b border-border-subtle/40"
            : "bg-transparent border-b border-transparent"
        }`}
      >
        <div className="w-full max-w-7xl mx-auto px-6 flex items-center justify-between">
          {/* Logo */}
          <a
            href="#hero"
            onClick={(e) => handleNavClick(e, "#hero")}
            className="flex-shrink-0 select-none"
          >
            <span className="text-xl font-bold tracking-[0.25em] text-text-primary hover:text-accent-orange transition-colors duration-200">
              TWISTERION
            </span>
          </a>

          {/* Desktop nav links */}
          <ul className="hidden md:flex items-center gap-8">
            {NAV_LINKS.map((link) => (
              <li key={link.href}>
                <a
                  href={link.href}
                  onClick={(e) => handleNavClick(e, link.href)}
                  className="text-sm font-medium tracking-wider text-text-muted hover:text-text-primary transition-colors duration-200 relative after:absolute after:bottom-[-4px] after:left-0 after:w-0 after:h-[2px] after:bg-accent-orange after:transition-all after:duration-300 hover:after:w-full"
                >
                  {link.label}
                </a>
              </li>
            ))}
          </ul>

          {/* Desktop CTA */}
          <a
            href="#waitlist"
            onClick={(e) => handleNavClick(e, "#waitlist")}
            className="hidden md:inline-flex items-center px-5 py-2 rounded-full text-sm font-semibold bg-accent-orange text-bg-primary hover:bg-accent-orange-hover hover:shadow-[0_0_20px_rgba(249,115,22,0.35)] transition-all duration-300"
          >
            Join the Waitlist
          </a>

          {/* Mobile hamburger */}
          <button
            type="button"
            aria-label={mobileOpen ? "Close menu" : "Open menu"}
            aria-expanded={mobileOpen}
            className="md:hidden relative w-8 h-8 flex items-center justify-center"
            onClick={() => setMobileOpen((prev) => !prev)}
          >
            <span className="sr-only">Toggle menu</span>
            <span
              className={`absolute h-[2px] w-5 bg-text-primary rounded transition-all duration-300 ${
                mobileOpen ? "rotate-45 translate-y-0" : "-translate-y-1.5"
              }`}
            />
            <span
              className={`absolute h-[2px] w-5 bg-text-primary rounded transition-all duration-300 ${
                mobileOpen ? "opacity-0 scale-x-0" : "opacity-100"
              }`}
            />
            <span
              className={`absolute h-[2px] w-5 bg-text-primary rounded transition-all duration-300 ${
                mobileOpen ? "-rotate-45 translate-y-0" : "translate-y-1.5"
              }`}
            />
          </button>
        </div>
      </nav>

      {/* Mobile slide-in overlay */}
      <div
        className={`fixed inset-0 z-40 md:hidden transition-opacity duration-300 ${
          mobileOpen ? "opacity-100 pointer-events-auto" : "opacity-0 pointer-events-none"
        }`}
      >
        {/* Backdrop */}
        <div
          className="absolute inset-0 bg-black/60 backdrop-blur-sm"
          onClick={() => setMobileOpen(false)}
        />

        {/* Slide panel */}
        <div
          className={`absolute top-16 right-0 w-64 h-[calc(100dvh-4rem)] bg-bg-surface border-l border-border-subtle flex flex-col transition-transform duration-300 ease-out ${
            mobileOpen ? "translate-x-0" : "translate-x-full"
          }`}
        >
          <ul className="flex flex-col py-6 px-6 gap-2">
            {NAV_LINKS.map((link) => (
              <li key={link.href}>
                <a
                  href={link.href}
                  onClick={(e) => handleNavClick(e, link.href)}
                  className="block py-3 px-4 text-sm font-medium tracking-wider text-text-muted hover:text-text-primary hover:bg-bg-elevated rounded-lg transition-colors duration-200"
                >
                  {link.label}
                </a>
              </li>
            ))}
          </ul>

          <div className="mt-auto px-6 pb-8">
            <a
              href="#waitlist"
              onClick={(e) => handleNavClick(e, "#waitlist")}
              className="flex items-center justify-center w-full py-3 rounded-full text-sm font-semibold bg-accent-orange text-bg-primary hover:bg-accent-orange-hover transition-colors duration-300"
            >
              Join the Waitlist
            </a>
          </div>
        </div>
      </div>
    </>
  );
}
