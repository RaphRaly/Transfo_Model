"use client";

import { useEffect, useRef } from "react";
import Image from "next/image";

export default function Hero() {
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

  const scrollTo = (e: React.MouseEvent<HTMLAnchorElement>, id: string) => {
    e.preventDefault();
    const target = document.querySelector(id);
    if (target) {
      target.scrollIntoView({ behavior: "smooth" });
    }
  };

  return (
    <section
      id="hero"
      ref={sectionRef}
      className="relative min-h-screen flex items-center justify-center overflow-hidden pt-16"
      style={{
        background:
          "radial-gradient(ellipse 80% 60% at 50% 45%, rgba(34,37,47,0.7) 0%, rgba(15,17,23,1) 100%)",
      }}
    >
      {/* Subtle grid overlay for depth */}
      <div
        className="pointer-events-none absolute inset-0 opacity-[0.03]"
        style={{
          backgroundImage:
            "linear-gradient(rgba(240,240,240,0.1) 1px, transparent 1px), linear-gradient(90deg, rgba(240,240,240,0.1) 1px, transparent 1px)",
          backgroundSize: "60px 60px",
        }}
      />

      {/* Accent glow blobs */}
      <div className="pointer-events-none absolute top-1/4 left-1/4 w-96 h-96 rounded-full bg-accent-cyan/5 blur-[120px]" />
      <div className="pointer-events-none absolute bottom-1/3 right-1/4 w-80 h-80 rounded-full bg-accent-orange/5 blur-[100px]" />

      <div className="relative z-10 w-full max-w-7xl mx-auto px-6 py-24 lg:py-32">
        <div className="flex flex-col-reverse lg:flex-row items-center gap-16 lg:gap-24">
          {/* Left: Text content */}
          <div className="flex-1 flex flex-col items-center lg:items-start text-center lg:text-left">
            {/* Tag line */}
            <span className="reveal-left inline-block mb-8 text-xs sm:text-sm font-medium uppercase tracking-[0.3em] text-accent-cyan">
              Powered by HysteriCore
            </span>

            {/* Headline */}
            <h1 className="reveal-left mb-8" style={{ transitionDelay: "100ms" }}>
              <span className="block text-4xl sm:text-5xl lg:text-6xl xl:text-7xl font-bold leading-[1.1] text-text-primary">
                The preamp that
                <br className="hidden sm:block" />
                {" doesn\u2019t exist in"}
                <br />
                {"hardware."}
              </span>
              <span className="block mt-2 text-4xl sm:text-5xl lg:text-6xl xl:text-7xl font-bold text-accent-orange">
                Yet.
              </span>
            </h1>

            {/* Subtitle */}
            <p
              className="reveal-left max-w-lg text-base sm:text-lg text-text-muted leading-[1.8] mb-12"
              style={{ transitionDelay: "200ms" }}
            >
              Physics-based transformer saturation &amp; dual-topology preamp
              plugin. Real Jiles-Atherton hysteresis modeling &mdash; not a
              waveshaper.
            </p>

            {/* CTAs */}
            <div
              className="reveal-left flex flex-col sm:flex-row items-center gap-4"
              style={{ transitionDelay: "300ms" }}
            >
              <a
                href="#features"
                onClick={(e) => scrollTo(e, "#features")}
                className="group inline-flex items-center justify-center px-7 py-3 rounded-full border border-border-accent text-sm font-semibold text-text-primary hover:border-accent-cyan hover:text-accent-cyan transition-all duration-300"
              >
                Discover
                <svg
                  className="ml-2 w-4 h-4 transition-transform duration-300 group-hover:translate-y-0.5"
                  fill="none"
                  viewBox="0 0 24 24"
                  stroke="currentColor"
                  strokeWidth={2}
                >
                  <path strokeLinecap="round" strokeLinejoin="round" d="M19 9l-7 7-7-7" />
                </svg>
              </a>

              <a
                href="#waitlist"
                onClick={(e) => scrollTo(e, "#waitlist")}
                className="inline-flex items-center justify-center px-7 py-3 rounded-full bg-accent-orange text-bg-primary text-sm font-semibold hover:bg-accent-orange-hover hover:shadow-[0_0_24px_rgba(249,115,22,0.4)] transition-all duration-300"
              >
                Join the Waitlist
              </a>
            </div>
          </div>

          {/* Right: Plugin GUI image */}
          <div className="flex-1 flex items-center justify-center w-full max-w-xl lg:max-w-none">
            <div
              className="reveal-right relative w-full"
              style={{ transitionDelay: "150ms" }}
            >
              <div
                className="animate-glow rounded-2xl overflow-hidden"
                style={{
                  transform: "perspective(1200px) rotateY(-5deg)",
                  transformStyle: "preserve-3d",
                }}
              >
                <Image
                  src="/twisterion-gui.png"
                  alt="TWISTERION plugin interface showing transformer saturation controls and dual-topology preamp"
                  width={800}
                  height={560}
                  priority
                  className="w-full h-auto rounded-2xl shadow-2xl shadow-black/40"
                  sizes="(max-width: 768px) 90vw, (max-width: 1024px) 50vw, 600px"
                />
              </div>

              {/* Reflection / ambient light under the image */}
              <div
                className="absolute -bottom-8 left-1/2 -translate-x-1/2 w-3/4 h-16 rounded-full bg-accent-cyan/8 blur-2xl pointer-events-none"
                aria-hidden="true"
              />
            </div>
          </div>
        </div>
      </div>

      {/* Scroll indicator */}
      <div className="absolute bottom-8 left-1/2 -translate-x-1/2 flex flex-col items-center gap-2 reveal" style={{ transitionDelay: "600ms" }}>
        <span className="text-xs text-text-dim tracking-widest uppercase">Scroll</span>
        <div className="w-5 h-8 rounded-full border border-border-accent flex items-start justify-center p-1">
          <div className="w-1 h-2 rounded-full bg-accent-orange animate-bounce" />
        </div>
      </div>
    </section>
  );
}
