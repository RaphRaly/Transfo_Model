import Navbar from "@/components/Navbar";
import Hero from "@/components/Hero";
import Features from "@/components/Features";
import TechCards from "@/components/TechCards";
import Specs from "@/components/Specs";
import AudioSection from "@/components/AudioSection";
import Footer from "@/components/Footer";

function SectionSpacer() {
  return (
    <div className="mx-auto max-w-xs py-8">
      <div className="h-px w-full bg-border-subtle/20" />
    </div>
  );
}

export default function Home() {
  return (
    <>
      <Navbar />
      <main>
        <Hero />
        <SectionSpacer />
        <Features />
        <SectionSpacer />
        <TechCards />
        <SectionSpacer />
        <Specs />
        <SectionSpacer />
        <AudioSection />
      </main>
      <Footer />
    </>
  );
}
