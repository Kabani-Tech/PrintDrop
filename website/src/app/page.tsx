import Link from "next/link"
import {
  Activity,
  ArrowRight,
  Cable,
  Cpu,
  FileText,
  Globe,
  HardDrive,
  KeyRound,
  Lightbulb,
  Radio,
  RefreshCw,
  Search,
  ShieldCheck,
  Usb,
  Wifi,
} from "lucide-react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { Separator } from "@/components/ui/separator"

const GITHUB_URL = "https://github.com/Kabani-Tech/PrintDrop"

const STATS = [
  { value: "1 016", label: "KB/s read over USB — measured" },
  { value: "~535", label: "KB/s write over USB — measured" },
  { value: "62", label: "KB web UI, self-hosted" },
  { value: "~16 MB/s", label: "raw card throughput — SDIO 4-bit" },
]

const FEATURES = [
  {
    icon: Usb,
    title: "An ordinary flash drive",
    description:
      "A USB mass storage device with a FAT32 volume. As far as the printer is concerned, nothing unusual is plugged in.",
  },
  {
    icon: Wifi,
    title: "A web UI over Wi-Fi",
    description:
      "Drag a .gcode file onto the page from any browser. Progress, transfer rate and estimated time are shown per file.",
  },
  {
    icon: ShieldCheck,
    title: "Local only",
    description:
      "Wi-Fi credentials live in the device's NVS. Nothing leaves your network, and there is no service to sign up for.",
  },
]

const STEPS = [
  {
    number: "01",
    title: "Upload from your desk",
    description:
      "Drag a .gcode file onto the web UI. Progress, transfer rate and estimated time are shown per file.",
  },
  {
    number: "02",
    title: "The card is handed back",
    description:
      "PrintDrop briefly withdraws the media from the printer while it writes, then re-presents it. The printer re-reads its file list.",
  },
  {
    number: "03",
    title: "Walk over and print",
    description:
      "The file appears in the printer's own menu, exactly as if it had been copied from a USB stick.",
  },
]

const PARTS = [
  "ESP32-S3",
  "TinyUSB MSC",
  "SD over SDIO (4-bit)",
  "FAT32",
  "LittleFS",
  "NVS",
  "No PSRAM",
  "One lock",
  "Local only",
]

const RULES = [
  {
    icon: Cable,
    title: "One lock",
    description:
      "Every access to the card, from either side, is serialised.",
  },
  {
    icon: Usb,
    title: "Withdraw before writing",
    description:
      "The media is taken away from the printer for the duration of a write, then handed back so it re-reads the table.",
  },
  {
    icon: RefreshCw,
    title: "Remount when the host writes",
    description:
      "If the printer changes sectors, the device throws away its own cached view before trusting it again.",
  },
]

const SIGNAL_PATH = [
  { icon: Globe, title: "Your browser", description: "Drag a .gcode onto the page" },
  { icon: Cpu, title: "ESP32-S3", description: "TinyUSB MSC · Wi-Fi" },
  { icon: HardDrive, title: "SD card", description: "FAT32 · one lock" },
  { icon: FileText, title: "Your printer", description: "Reads its USB menu" },
]

const UX = [
  {
    icon: Radio,
    title: "WebSocket progress",
    description: "ws://:81 pushes upload progress and status — no 5 s poll, every client sees the same bar.",
  },
  {
    icon: Lightbulb,
    title: "LED + button",
    description: "Idle 2 s heartbeat, activity fast blink, error double-blink. Short press = eject, 5 s = factory reset.",
  },
  {
    icon: Search,
    title: "mDNS + LLMNR",
    description: "http://printdrop.local (mDNS) and http://printdrop (LLMNR) — Windows without Bonjour still finds it.",
  },
  {
    icon: KeyRound,
    title: "Web UI auth",
    description: "HTTP Basic, SHA-256 in NVS, seed from ini, change via serial auth or Settings.",
  },
  {
    icon: Activity,
    title: "Dual OTA",
    description: "POST /api/ota with .bin or drop firmware.bin+.json on SD — dual 1 344 KB slots on 4 MB.",
  },
]

const DOCS = [
  {
    href: "/docs/getting-started/",
    title: "Getting started",
    description: "Hardware, wiring, flashing and first run",
  },
  {
    href: "/docs/architecture/",
    title: "Architecture",
    description: "How USB and Wi-Fi share one card",
  },
  {
    href: "/docs/hardware/",
    title: "Hardware",
    description: "The board as measured, wiring and power",
  },
  {
    href: "/docs/sdio/",
    title: "SDIO 4-bit",
    description: "6-wire migration, 6 s vs 80 s uploads",
  },
  {
    href: "/docs/authentication/",
    title: "Authentication",
    description: "HTTP Basic, SHA-256 NVS, Basic header",
  },
  {
    href: "/docs/discovery/",
    title: "Discovery",
    description: "mDNS + LLMNR, printdrop.local / printdrop",
  },
  {
    href: "/docs/ota/",
    title: "OTA update",
    description: "Dual 1 344 KB slots, HTTP + SD card",
  },
  {
    href: "/docs/bugs/",
    title: "Bugs found",
    description: "Every fault hit during the port, and why",
  },
  {
    href: "/docs/flashing/",
    title: "Flashing",
    description: "Download mode on this board",
  },
  {
    href: "/docs/contributing/",
    title: "Contributing",
    description: "Build setup, branch model, what to test",
  },
]

export default function Home() {
  return (
    <>
      {/* ---------------------------------------------------------- hero */}
      <section className="relative overflow-hidden border-b">
        <div className="grid-surface pointer-events-none absolute inset-0" />
        <div className="relative mx-auto grid max-w-7xl gap-12 px-4 py-24 sm:px-6 sm:py-32 lg:grid-cols-[1.15fr_.85fr] lg:items-center lg:py-40">
          <div>
            <Badge variant="secondary" className="mb-6 bg-background/70">
              Open source · Local first · No cloud
            </Badge>
            <h1 className="max-w-4xl text-5xl leading-[0.98] font-semibold tracking-[-0.055em] sm:text-7xl">
              Drop a print job from your desk.
            </h1>
            <p className="mt-7 max-w-2xl text-lg leading-8 text-muted-foreground sm:text-xl">
              PrintDrop plugs into your printer&apos;s USB port and appears as an
              ordinary flash drive — while also serving a web UI over Wi-Fi.
              No cloud, no account, no firmware changes.
            </p>
            <div className="mt-9 flex flex-wrap gap-3">
              <Button size="lg" asChild>
                <a href={GITHUB_URL} target="_blank" rel="noopener">
                  View on GitHub <ArrowRight />
                </a>
              </Button>
              <Button size="lg" variant="outline" asChild>
                <Link href="/#how">How it works</Link>
              </Button>
            </div>
            <p className="mt-5 text-sm text-muted-foreground">
              Runs on a 4 MB ESP32-S3. Built for printers that only accept USB
              media.
            </p>
          </div>

          <Card className="overflow-hidden bg-card/90 shadow-2xl shadow-black">
            <CardHeader className="border-b">
              <div className="flex items-center justify-between">
                <div className="flex items-center gap-2 text-sm text-muted-foreground">
                  <Usb className="size-4" />
                  printdrop.local
                </div>
                <Badge variant="secondary">
                  <span className="size-1.5 rounded-full bg-white" />
                  media online
                </Badge>
              </div>
            </CardHeader>
            <CardContent className="p-0">
              <div className="relative bg-black p-5 font-mono text-[13px] leading-6 text-zinc-300">
                <span className="text-zinc-600">$ </span>printfd status
                <br />
                bus&nbsp;&nbsp;&nbsp;sdio 4-bit @ 20 MHz · fat32 — projected
                <br />
                wifi&nbsp;&nbsp;printdrop.local · 192.168.1.42
                <br />
                lock&nbsp;&nbsp;idle · printer owns the card
              </div>
              <div className="grid gap-3 p-5">
                <div className="flex items-start gap-3 rounded-lg border bg-background p-4">
                  <FileText className="mt-0.5 size-4 shrink-0 text-muted-foreground" />
                  <div>
                    <p className="text-sm font-medium">benchy.gcode</p>
                    <p className="mt-1 text-sm text-muted-foreground">
                      20 MB · read by the printer at ~1 016 KB/s
                    </p>
                  </div>
                  <Badge variant="outline" className="ml-auto">
                    ready
                  </Badge>
                </div>
                <div className="flex items-center gap-2 text-xs text-muted-foreground">
                  <HardDrive className="size-3.5" />
                  Withdrawn from the printer while writing, remounted after
                </div>
              </div>
            </CardContent>
          </Card>
        </div>
      </section>

      {/* --------------------------------------------------------- stats */}
      <section id="performance" className="mx-auto max-w-7xl px-4 py-10 sm:px-6">
        <div className="grid divide-y rounded-xl border bg-card sm:grid-cols-4 sm:divide-x sm:divide-y-0">
          {STATS.map((stat) => (
            <div key={stat.label} className="px-6 py-5">
              <p className="text-2xl font-semibold tracking-tight tabular-nums">
                {stat.value}
              </p>
              <p className="text-sm text-muted-foreground">{stat.label}</p>
            </div>
          ))}
        </div>
        <p className="mt-4 text-sm text-muted-foreground">
          <span className="font-medium text-foreground">All measured on hardware</span>{" "}
          — an ESP32-S3 with a 32&nbsp;GB SDHC card on SDIO 4-bit at 40&nbsp;MHz.
          The card sustains ~16&nbsp;MB/s raw, against ~910&nbsp;KB/s on the old
          SPI wiring. Over USB the device reads at 1&nbsp;016&nbsp;KB/s and
          writes at ~535&nbsp;KB/s (SPI managed 485 / 248).
        </p>
        <p className="mt-3 text-sm text-muted-foreground">
          Reads are now at the ceiling of the ESP32-S3&apos;s{" "}
          <span className="font-medium text-foreground">USB Full-Speed</span>{" "}
          peripheral — 12&nbsp;Mbit/s, about 1.2&nbsp;MB/s — so the card has
          plenty of headroom to spare and further USB gains would need different
          silicon rather than different firmware. Uploads over Wi-Fi run at
          ~200&nbsp;KB/s and downloads at ~500&nbsp;KB/s, bounded by the HTTP
          path in firmware rather than by the card or the radio. The old SPI
          driver remains available as{" "}
          <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">-e printdrop_spi</code>.
        </p>
      </section>

      {/* ----------------------------------------------------------- why */}
      <section id="why" className="mx-auto max-w-7xl px-4 py-24 sm:px-6">
        <div className="max-w-2xl">
          <Badge variant="secondary">Why PrintDrop</Badge>
          <h2 className="mt-5 text-4xl font-semibold tracking-[-0.04em] sm:text-5xl">
            The errand the USB stick keeps creating.
          </h2>
          <p className="mt-5 text-lg leading-8 text-muted-foreground">
            Plenty of 3D printers accept jobs only from USB media and have no
            networking of their own. Every print becomes the same errand: slice
            at your desk, copy to a stick, carry it across the workshop — then
            somebody else needs the stick.
          </p>
        </div>
        <div className="mt-12 grid gap-4 md:grid-cols-3">
          {FEATURES.map((feature) => (
            <Card key={feature.title}>
              <CardHeader>
                <div className="mb-3 flex size-9 items-center justify-center rounded-md border bg-background">
                  <feature.icon className="size-4" />
                </div>
                <CardTitle>{feature.title}</CardTitle>
                <CardDescription className="leading-6">
                  {feature.description}
                </CardDescription>
              </CardHeader>
            </Card>
          ))}
        </div>
      </section>

      <Separator />

      {/* ----------------------------------------------------------- how */}
      <section id="how" className="mx-auto max-w-7xl px-4 py-24 sm:px-6">
        <div className="grid gap-12 lg:grid-cols-2 lg:items-center">
          <div>
            <Badge variant="secondary">How it works</Badge>
            <h2 className="mt-5 text-4xl font-semibold tracking-[-0.04em] sm:text-5xl">
              Drop. Hand back. Print.
            </h2>
            <p className="mt-5 text-lg leading-8 text-muted-foreground">
              PrintDrop holds the card, serves it to both sides, and never lets
              them collide. While a file arrives over Wi-Fi the media is
              withdrawn from the printer; when the write is done it is
              re-presented, and the new job is simply in the menu.
            </p>
            <div className="mt-8 flex flex-wrap gap-3">
              <Button asChild>
                <Link href={DOCS[1].href}>
                  Read the architecture <ArrowRight />
                </Link>
              </Button>
              <Button variant="outline" asChild>
                <Link href={DOCS[0].href}>Getting started</Link>
              </Button>
            </div>
          </div>
          <Card className="bg-black shadow-sm">
            <CardContent className="p-5 font-mono text-[13px] leading-6 text-zinc-300">
              <pre className="overflow-x-auto">
                <code>
                  {`[desk]     drop benchy.gcode → web ui
[device]   media withdrawn · writing 20 MB
[device]   write done · media re-presented
[printer]  FAT re-read · job in the USB menu`}
                </code>
              </pre>
            </CardContent>
          </Card>
        </div>

        <div className="mt-12 grid gap-4 md:grid-cols-3">
          {STEPS.map((step) => (
            <Card key={step.number}>
              <CardHeader>
                <span className="font-mono text-xs tracking-[0.08em] text-muted-foreground">
                  {step.number}
                </span>
                <CardTitle className="mt-2">{step.title}</CardTitle>
                <CardDescription className="leading-6">
                  {step.description}
                </CardDescription>
              </CardHeader>
            </Card>
          ))}
        </div>
      </section>

      {/* -------------------------------------------------------- inside */}
      <section id="inside" className="border-y bg-card/40">
        <div className="mx-auto grid max-w-7xl gap-10 px-4 py-24 sm:px-6 lg:grid-cols-[.75fr_1.25fr]">
          <div>
            <Badge>Inside the device</Badge>
            <h2 className="mt-5 text-4xl font-semibold tracking-[-0.04em]">
              Small parts. No surprises.
            </h2>
            <p className="mt-5 leading-7 text-muted-foreground">
              A 4 MB, no-PSRAM ESP32-S3 drives an SD card over{" "}
              <span className="font-medium text-foreground">
                SDIO 4-bit at 20 MHz (40 MHz with short wiring)
              </span>{" "}
              and presents it through TinyUSB mass storage — ~4× the
              SPI bandwidth on the same breakout plus two wires, ~6×
              at 40 MHz. SPI remains available as{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">-e printdrop_spi</code>.
              The UI is 62 KB of plain HTML, CSS and JavaScript served
              from a LittleFS partition — no CDN, no external requests,
              no build step.
            </p>
            <Button variant="outline" className="mt-7" asChild>
              <Link href={DOCS[2].href}>
                Hardware notes <ArrowRight />
              </Link>
            </Button>
          </div>
          <div className="flex content-start flex-wrap gap-2">
            {PARTS.map((part) => (
              <Badge
                key={part}
                variant="secondary"
                className="px-3 py-1.5 text-sm"
              >
                {part}
              </Badge>
            ))}
          </div>
        </div>
      </section>

      {/* ------------------------------------------------------ hard part */}
      <section className="mx-auto max-w-7xl px-4 py-24 sm:px-6">
        <div className="mx-auto max-w-2xl text-center">
          <Badge variant="secondary">Correctness</Badge>
          <h2 className="mt-5 text-4xl font-semibold tracking-[-0.04em] sm:text-5xl">
            One card. Two masters.
          </h2>
          <p className="mt-5 leading-7 text-muted-foreground">
            A USB host caches the file allocation table. Writing to the card
            behind its back is how these devices normally corrupt themselves.
            PrintDrop holds to three rules.
          </p>
        </div>
        <div className="mt-12 grid gap-4 md:grid-cols-3">
          {RULES.map((rule) => (
            <Card key={rule.title}>
              <CardHeader>
                <div className="mb-3 flex size-9 items-center justify-center rounded-md border bg-background">
                  <rule.icon className="size-4" />
                </div>
                <CardTitle>{rule.title}</CardTitle>
                <CardDescription className="leading-6">
                  {rule.description}
                </CardDescription>
              </CardHeader>
            </Card>
          ))}
        </div>
        <p className="mx-auto mt-8 max-w-2xl text-center text-sm text-muted-foreground">
          Mass storage callbacks take the lock with a short timeout and fail
          the transfer rather than block, so a slow upload can never stall a
          printer mid-job.
        </p>

        <div className="mt-16 grid gap-3 lg:grid-cols-[1fr_auto_1fr_auto_1fr_auto_1fr] lg:items-center">
          {SIGNAL_PATH.map((node, index) => (
            <div key={node.title} className="contents">
              {index > 0 && (
                <ArrowRight
                  className="mx-auto hidden size-5 text-muted-foreground lg:block"
                  aria-hidden
                />
              )}
              <Card className="gap-3 py-5">
                <CardHeader>
                  <node.icon className="mb-1 size-5 text-muted-foreground" />
                  <CardTitle className="text-base">{node.title}</CardTitle>
                  <CardDescription>{node.description}</CardDescription>
                </CardHeader>
              </Card>
            </div>
          ))}
        </div>
      </section>

      {/* ----------------------------------------------------------- experience */}
      <section className="border-y bg-card/40">
        <div className="mx-auto max-w-7xl px-4 py-24 sm:px-6">
          <div className="max-w-2xl">
            <Badge>The experience</Badge>
            <h2 className="mt-5 text-4xl font-semibold tracking-[-0.04em]">
              Five small pieces, one coherent UX.
            </h2>
            <p className="mt-5 leading-7 text-muted-foreground">
              Live progress over WebSocket, a heartbeat LED you can read across the room, zero-config discovery that
              actually works on Windows, a login that lives in NVS, and an OTA path that survives a bad flash — all
              on the same 4 MB stick.
            </p>
          </div>
          <div className="mt-12 grid gap-4 md:grid-cols-3 lg:grid-cols-5">
            {UX.map((f) => (
              <Card key={f.title}>
                <CardHeader>
                  <div className="mb-3 flex size-9 items-center justify-center rounded-md border bg-background">
                    <f.icon className="size-4" />
                  </div>
                  <CardTitle className="text-sm">{f.title}</CardTitle>
                  <CardDescription className="leading-6 text-xs">{f.description}</CardDescription>
                </CardHeader>
              </Card>
            ))}
          </div>
        </div>
      </section>

      {/* ----------------------------------------------------------- docs */}
      <section id="docs" className="mx-auto max-w-7xl px-4 py-24 sm:px-6">
        <div className="max-w-2xl">
          <Badge variant="secondary">Documentation</Badge>
          <h2 className="mt-5 text-4xl font-semibold tracking-[-0.04em] sm:text-5xl">
            Every detail, written down.
          </h2>
        </div>
        <div className="mt-12 grid gap-4 md:grid-cols-2 lg:grid-cols-3">
          {DOCS.map((doc) => (
            <Link key={doc.title} href={doc.href} className="group">
              <Card className="h-full transition-colors group-hover:bg-accent/50">
                <CardHeader>
                  <CardTitle className="flex items-center justify-between gap-2">
                    {doc.title}
                    <ArrowRight className="size-4 text-muted-foreground transition-transform group-hover:translate-x-0.5" />
                  </CardTitle>
                  <CardDescription className="leading-6">
                    {doc.description}
                  </CardDescription>
                </CardHeader>
              </Card>
            </Link>
          ))}
        </div>
      </section>

      {/* ----------------------------------------------------------- CTA */}
      <section className="mx-auto max-w-7xl px-4 py-12 sm:px-6">
        <Card className="items-center bg-primary py-12 text-center text-primary-foreground">
          <CardHeader className="mx-auto max-w-3xl px-6">
            <CardTitle className="text-3xl tracking-[-0.035em] sm:text-4xl">
              Stop carrying a stick across the workshop.
            </CardTitle>
            <CardDescription className="mt-3 text-base text-primary-foreground/70">
              Flash an ESP32-S3, plug it into the printer&apos;s USB port, and
              drop jobs from any browser on your network.
            </CardDescription>
          </CardHeader>
          <CardContent className="flex flex-wrap justify-center gap-3 px-6">
            <Button variant="secondary" asChild>
              <Link href={DOCS[0].href}>
                Getting started
              </Link>
            </Button>
            <Button
              variant="outline"
              asChild
              className="border-primary-foreground/25 bg-transparent text-primary-foreground hover:bg-primary-foreground/10 hover:text-primary-foreground"
            >
              <a href={GITHUB_URL} target="_blank" rel="noopener">
                View source
              </a>
            </Button>
          </CardContent>
        </Card>
      </section>
    </>
  )
}
