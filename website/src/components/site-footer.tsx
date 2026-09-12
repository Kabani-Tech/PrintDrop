import Link from "next/link"

import { Separator } from "@/components/ui/separator"

const GITHUB_URL = "https://github.com/Kabani-Tech/PrintDrop"
const COFFEE_URL = "https://buymeacoffee.com/akash97p"

const FOOTER_LINKS = [
  { href: "/docs/", label: "Docs" },
  { href: "/docs/contributing/", label: "Contributing" },
  { href: GITHUB_URL, label: "Source" },
  { href: COFFEE_URL, label: "Buy me a coffee" },
]

export function SiteFooter() {
  return (
    <footer className="mx-auto max-w-7xl px-4 pt-16 pb-10 sm:px-6">
      <Separator />
      <div className="flex flex-col gap-4 pt-6 text-sm text-muted-foreground sm:flex-row sm:items-center sm:justify-between">
        <p>
          PrintDrop · Akash P — CTO,{" "}
          <a
            href="https://kabanitech.com"
            target="_blank"
            rel="noopener"
            className="transition-colors hover:text-foreground"
          >
            Kabani Tech Private Limited
          </a>{" "}
          · MIT License
        </p>
        <nav className="flex gap-5" aria-label="Footer navigation">
          {FOOTER_LINKS.map((link) => (
            <Link
              key={link.label}
              href={link.href}
              className="transition-colors hover:text-foreground"
            >
              {link.label}
            </Link>
          ))}
        </nav>
      </div>
    </footer>
  )
}
