import type { Metadata } from "next";
import { Space_Grotesk } from "next/font/google";
import "./globals.css";

const spaceGrotesk = Space_Grotesk({
  variable: "--font-space-grotesk",
  subsets: ["latin"],
  weight: ["300", "400", "500", "600", "700"],
});

export const metadata: Metadata = {
  title: "TWISTERION - Physics-Based Transformer Saturation Plugin",
  description:
    "Powered by HysteriCore. Real magnetic hysteresis modeling with dual-topology preamp. VST3, AU, AAX.",
  openGraph: {
    title: "TWISTERION - Powered by HysteriCore",
    description:
      "Physics-based transformer saturation & dual-topology preamp plugin. Not a waveshaper. Real Jiles-Atherton hysteresis.",
    images: ["/twisterion-gui.png"],
    type: "website",
  },
  twitter: {
    card: "summary_large_image",
    title: "TWISTERION - Powered by HysteriCore",
    description:
      "Physics-based transformer saturation & dual-topology preamp plugin.",
    images: ["/twisterion-gui.png"],
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en" className={`${spaceGrotesk.variable} h-full`}>
      <body className="min-h-full flex flex-col font-sans antialiased">
        {children}
      </body>
    </html>
  );
}
