"use client";

import { useEffect, useRef } from "react";

/* ------------------------------------------------------------------ */
/*  Card data                                                         */
/* ------------------------------------------------------------------ */

type Accent = "cyan" | "orange" | "green" | "yellow" | "violet";

interface TechCard {
  icon: React.ReactNode;
  title: string;
  subtitle: string;
  description: string;
  accent: Accent;
}

const ACCENT_MAP: Record<Accent, { border: string; text: string; top: string }> = {
  cyan:   { border: "hover:border-accent-cyan",   text: "text-accent-cyan",   top: "bg-accent-cyan"   },
  orange: { border: "hover:border-accent-orange", text: "text-accent-orange", top: "bg-accent-orange" },
  green:  { border: "hover:border-accent-green",  text: "text-accent-green",  top: "bg-accent-green"  },
  yellow: { border: "hover:border-accent-yellow", text: "text-accent-yellow", top: "bg-accent-yellow" },
  violet: { border: "hover:border-accent-violet", text: "text-accent-violet", top: "bg-accent-violet" },
};

/* ---- Inline SVG icons -------------------------------------------- */

function IconMEJunction({ className }: { className?: string }) {
  return (
    <svg className={className} viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
      <circle cx="12" cy="20" r="8" stroke="currentColor" strokeWidth="2" />
      <circle cx="28" cy="20" r="8" stroke="currentColor" strokeWidth="2" />
      <line x1="20" y1="20" x2="20" y2="20" stroke="currentColor" strokeWidth="2" />
      <path d="M16 16L24 24M24 16L16 24" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.5" />
    </svg>
  );
}

function IconHSIM({ className }: { className?: string }) {
  return (
    <svg className={className} viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M6 20H14" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
      <path d="M26 20H34" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
      <path d="M20 6V14" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
      <path d="M20 26V34" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
      <path d="M14 20L20 16L26 20L20 24Z" fill="currentColor" opacity="0.25" stroke="currentColor" strokeWidth="1.5" strokeLinejoin="round" />
      <path d="M9 9L15 15" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.6" />
      <path d="M25 25L31 31" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.6" />
      <path d="M31 9L25 15" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.6" />
      <path d="M9 31L15 25" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.6" />
    </svg>
  );
}

function IconCPWL({ className }: { className?: string }) {
  return (
    <svg className={className} viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
      <polyline
        points="4,32 10,28 16,18 20,14 24,12 28,11 34,10"
        stroke="currentColor"
        strokeWidth="2"
        strokeLinecap="round"
        strokeLinejoin="round"
        fill="none"
      />
      <circle cx="10" cy="28" r="2" fill="currentColor" opacity="0.5" />
      <circle cx="16" cy="18" r="2" fill="currentColor" opacity="0.5" />
      <circle cx="20" cy="14" r="2" fill="currentColor" opacity="0.5" />
      <circle cx="24" cy="12" r="2" fill="currentColor" opacity="0.5" />
      <circle cx="28" cy="11" r="2" fill="currentColor" opacity="0.5" />
    </svg>
  );
}

function IconBertotti({ className }: { className?: string }) {
  return (
    <svg className={className} viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path
        d="M4 20C7 14 10 26 13 20C16 14 19 26 22 20C25 16 28 22 31 20C34 18 37 20 37 20"
        stroke="currentColor"
        strokeWidth="2"
        strokeLinecap="round"
        fill="none"
      />
      <path
        d="M4 20C7 17 10 23 13 20C16 17 19 23 22 20"
        stroke="currentColor"
        strokeWidth="1"
        strokeLinecap="round"
        fill="none"
        opacity="0.3"
        transform="translate(0, 8)"
      />
      <line x1="4" y1="34" x2="37" y2="34" stroke="currentColor" strokeWidth="1" opacity="0.2" />
    </svg>
  );
}

function IconTMT({ className }: { className?: string }) {
  return (
    <svg className={className} viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
      <path d="M14 8V32" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
      <path d="M26 8V32" stroke="currentColor" strokeWidth="2" strokeLinecap="round" />
      <path d="M14 14L8 10" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.7" />
      <path d="M14 20L6 20" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.7" />
      <path d="M14 26L8 30" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.7" />
      <path d="M26 14L32 10" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.7" />
      <path d="M26 20L34 20" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.7" />
      <path d="M26 26L32 30" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" opacity="0.7" />
      <text x="11" y="6" fontSize="7" fill="currentColor" fontWeight="bold" opacity="0.6">L</text>
      <text x="24" y="6" fontSize="7" fill="currentColor" fontWeight="bold" opacity="0.6">R</text>
    </svg>
  );
}

function IconWDF({ className }: { className?: string }) {
  return (
    <svg className={className} viewBox="0 0 40 40" fill="none" xmlns="http://www.w3.org/2000/svg">
      <circle cx="10" cy="12" r="3" stroke="currentColor" strokeWidth="1.5" />
      <circle cx="30" cy="12" r="3" stroke="currentColor" strokeWidth="1.5" />
      <circle cx="10" cy="28" r="3" stroke="currentColor" strokeWidth="1.5" />
      <circle cx="30" cy="28" r="3" stroke="currentColor" strokeWidth="1.5" />
      <circle cx="20" cy="20" r="4" stroke="currentColor" strokeWidth="2" fill="currentColor" fillOpacity="0.15" />
      <line x1="13" y1="12" x2="17" y2="18" stroke="currentColor" strokeWidth="1.5" />
      <line x1="27" y1="12" x2="23" y2="18" stroke="currentColor" strokeWidth="1.5" />
      <line x1="13" y1="28" x2="17" y2="22" stroke="currentColor" strokeWidth="1.5" />
      <line x1="27" y1="28" x2="23" y2="22" stroke="currentColor" strokeWidth="1.5" />
    </svg>
  );
}

/* ---- Card definitions -------------------------------------------- */

const CARDS: TechCard[] = [
  {
    icon: <IconMEJunction className="w-10 h-10" />,
    title: "MEJunction",
    subtitle: "Magneto-Electric Coupling",
    description:
      "Novel WDF element converting between electrical and magnetic wave domains via Faraday\u2019s and Amp\u00e8re\u2019s laws.",
    accent: "cyan",
  },
  {
    icon: <IconHSIM className="w-10 h-10" />,
    title: "HSIM Solver",
    subtitle: "Hybrid Scattering-Impedance",
    description:
      "Alternates wave-domain scans with adaptive port resistance. Sherman\u2013Morrison O(N\u00b2) updates every 16 samples.",
    accent: "orange",
  },
  {
    icon: <IconCPWL className="w-10 h-10" />,
    title: "CPWL + ADAA",
    subtitle: "Zero-Oversampling Realtime",
    description:
      "Piecewise-linear hysteresis with analytical antiderivative antialiasing. Full alias suppression, zero CPU overhead.",
    accent: "green",
  },
  {
    icon: <IconBertotti className="w-10 h-10" />,
    title: "Dynamic Losses",
    subtitle: "Bertotti Field Separation",
    description:
      "Frequency-dependent eddy current and excess losses. Concentrates distortion at low frequencies, preserves HF transparency.",
    accent: "yellow",
  },
  {
    icon: <IconTMT className="w-10 h-10" />,
    title: "TMT Spread",
    subtitle: "Stereo Variation",
    description:
      "Component tolerance modeling between L/R channels. Natural stereo width without phase tricks.",
    accent: "violet",
  },
  {
    icon: <IconWDF className="w-10 h-10" />,
    title: "WDF Topology",
    subtitle: "Wave Digital Filters",
    description:
      "Complete transformer circuit discretized in the wave domain. Passivity guaranteed by design \u2014 always stable.",
    accent: "cyan",
  },
];

/* ------------------------------------------------------------------ */
/*  Component                                                         */
/* ------------------------------------------------------------------ */

export default function TechCards() {
  const sectionRef = useRef<HTMLElement>(null);

  useEffect(() => {
    const el = sectionRef.current;
    if (!el) return;

    const observer = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            entry.target.classList.add("visible");
          }
        });
      },
      { threshold: 0.15, rootMargin: "0px 0px -60px 0px" }
    );

    const revealEls = el.querySelectorAll(".reveal");
    revealEls.forEach((r) => observer.observe(r));

    return () => observer.disconnect();
  }, []);

  return (
    <section
      id="technology"
      ref={sectionRef}
      className="relative px-6 max-w-7xl mx-auto"
      style={{ paddingTop: '12rem', paddingBottom: '12rem' }}
    >
      {/* ---- Header ---- */}
      <div className="text-center mb-24 reveal">
        <p className="uppercase tracking-widest text-accent-orange text-sm font-semibold mb-3">
          Under the Hood
        </p>
        <h2 className="text-4xl font-bold text-text-primary mb-4">
          The Technology
        </h2>
        <p className="text-text-muted max-w-2xl mx-auto text-lg leading-relaxed">
          Every component is a Wave Digital Filter element. The saturation
          emerges from the physics.
        </p>
      </div>

      {/* ---- Cards grid ---- */}
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-10">
        {CARDS.map((card, i) => {
          const a = ACCENT_MAP[card.accent];
          return (
            <div
              key={card.title}
              className={`reveal group relative bg-bg-surface/50 border border-border-subtle/60 rounded-2xl p-8 transition-all duration-500 ease-out hover:-translate-y-1 ${a.border}`}
              style={{ transitionDelay: `${i * 100}ms` }}
            >
              {/* Accent top bar */}
              <div
                className={`absolute top-0 left-0 right-0 h-[3px] rounded-t-xl ${a.top}`}
              />

              {/* Icon */}
              <div className={`mb-5 ${a.text}`}>{card.icon}</div>

              {/* Title */}
              <h3 className="text-lg font-bold text-text-primary mb-1">
                {card.title}
              </h3>

              {/* Subtitle */}
              <p className={`text-sm font-medium mb-4 ${a.text}`}>
                {card.subtitle}
              </p>

              {/* Description */}
              <p className="text-sm text-text-muted leading-relaxed">
                {card.description}
              </p>
            </div>
          );
        })}
      </div>
    </section>
  );
}
