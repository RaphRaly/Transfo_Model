"use client";

import { useEffect, useRef } from "react";

const DEMO_CARDS = [
  { title: "Clean vs Driven", id: "clean-driven" },
  { title: "Heritage Mode", id: "heritage" },
  { title: "Modern Mode", id: "modern" },
] as const;

export default function AudioSection() {
  const sectionRef = useRef<HTMLElement>(null);

  useEffect(() => {
    const section = sectionRef.current;
    if (!section) return;

    const reveals = section.querySelectorAll(".reveal, .reveal-left, .reveal-right");

    const observer = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            entry.target.classList.add("visible");
          }
        });
      },
      { threshold: 0.15, rootMargin: "0px 0px -40px 0px" }
    );

    reveals.forEach((el) => observer.observe(el));

    return () => observer.disconnect();
  }, []);

  const handleSubmit = (e: React.FormEvent<HTMLFormElement>) => {
    e.preventDefault();
  };

  return (
    <section
      id="audio"
      ref={sectionRef}
      className="bg-bg-surface"
    >
      <div className="max-w-7xl mx-auto px-6" style={{ paddingTop: '12rem', paddingBottom: '12rem' }}>
        {/* Section header */}
        <div className="text-center mb-20">
          <span className="reveal inline-block text-xs font-medium uppercase tracking-widest text-accent-violet mb-4">
            Listen
          </span>
          <h2
            className="reveal text-4xl font-bold text-text-primary mb-4"
            style={{ transitionDelay: "100ms" }}
          >
            Hear the Difference
          </h2>
          <p
            className="reveal text-text-muted max-w-xl mx-auto leading-relaxed"
            style={{ transitionDelay: "200ms" }}
          >
            Sound demos coming soon. Join the waitlist to be notified.
          </p>
        </div>

        {/* Demo cards */}
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-8 mb-24">
          {DEMO_CARDS.map((card, i) => (
            <div
              key={card.id}
              className="reveal group bg-bg-elevated/50 rounded-2xl border border-border-subtle/60
                         flex flex-col items-center justify-center h-[220px]
                         hover:border-accent-violet hover:scale-[1.02]
                         transition-all duration-300 cursor-default"
              style={{ transitionDelay: `${300 + i * 100}ms` }}
            >
              {/* Play button circle */}
              <div className="w-[60px] h-[60px] rounded-full border-2 border-accent-violet
                              flex items-center justify-center mb-4
                              group-hover:bg-accent-violet/10 transition-colors duration-300">
                <svg
                  className="w-5 h-5 text-accent-violet ml-0.5"
                  viewBox="0 0 24 24"
                  fill="currentColor"
                >
                  <path d="M8 5.14v14l11-7-11-7z" />
                </svg>
              </div>

              {/* Card title */}
              <span className="text-sm font-medium text-text-primary mb-2">
                {card.title}
              </span>

              {/* Coming Soon badge */}
              <span className="text-xs bg-bg-surface text-text-dim rounded-full px-3 py-1">
                Coming Soon
              </span>
            </div>
          ))}
        </div>

        {/* Email signup / waitlist form */}
        <div
          id="waitlist"
          className="reveal max-w-md mx-auto text-center"
          style={{ transitionDelay: "600ms" }}
        >
          <h3 className="text-lg font-semibold text-text-primary mb-5">
            Get notified at launch
          </h3>

          <form onSubmit={handleSubmit} className="flex">
            <input
              type="email"
              placeholder="your@email.com"
              className="flex-1 min-w-0 bg-bg-primary border border-border-subtle rounded-l-lg
                         px-4 py-3 text-text-primary text-sm placeholder:text-text-dim
                         outline-none focus:border-accent-orange transition-colors duration-200"
            />
            <button
              type="submit"
              className="bg-accent-orange text-white font-semibold rounded-r-lg
                         px-6 py-3 text-sm whitespace-nowrap
                         hover:bg-accent-orange-hover
                         hover:shadow-[0_0_20px_rgba(249,115,22,0.3)]
                         transition-all duration-300"
            >
              Join Waitlist
            </button>
          </form>

          <p className="text-xs text-text-dim mt-3">
            No spam. Launch announcement only.
          </p>
        </div>
      </div>
    </section>
  );
}
