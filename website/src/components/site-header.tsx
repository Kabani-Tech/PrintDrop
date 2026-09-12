"use client"

import { useState } from "react"
import Image from "next/image"
import Link from "next/link"
import { Coffee, GitBranch, Menu } from "lucide-react"

import { Button } from "@/components/ui/button"
import {
  Sheet,
  SheetClose,
  SheetContent,
  SheetHeader,
  SheetTitle,
  SheetTrigger,
} from "@/components/ui/sheet"

const GITHUB_URL = "https://github.com/Kabani-Tech/PrintDrop"
const COFFEE_URL = "https://buymeacoffee.com/akash97p"

const NAV_LINKS = [
  { href: "/#why", label: "Why" },
  { href: "/#how", label: "How it works" },
  { href: "/#inside", label: "Inside" },
  { href: "/#performance", label: "Performance" },
  { href: "/devices", label: "Devices" },
  { href: "/flash", label: "Flash Firmware" },
  { href: "/docs/", label: "Docs" },
]

export function SiteHeader() {
  const [open, setOpen] = useState(false)
  const basePath =
    process.env.NEXT_BASE_PATH ??
    (process.env.NODE_ENV === "production" ? "/PrintDrop" : "")

  return (
    <header className="sticky top-0 z-40 border-b bg-background/95 backdrop-blur supports-[backdrop-filter]:bg-background/80">
      <div className="mx-auto flex h-14 max-w-7xl items-center px-4 sm:px-6">
        <Link
          href="/"
          className="flex items-center gap-2 font-semibold tracking-tight"
        >
          <Image
            src={`${basePath}/logo.webp`}
            alt=""
            width={26}
            height={26}
            className="rounded-md"
          />
          PrintDrop
        </Link>

        <nav
          className="ml-8 hidden items-center gap-6 text-sm md:flex"
          aria-label="Primary navigation"
        >
          {NAV_LINKS.map((link) => (
            <Link
              key={link.href}
              href={link.href}
              className="text-muted-foreground transition-colors hover:text-foreground"
            >
              {link.label}
            </Link>
          ))}
        </nav>

        <div className="ml-auto flex items-center gap-1">
          <Button variant="ghost" size="sm" asChild className="hidden sm:inline-flex">
            <a href={GITHUB_URL} target="_blank" rel="noopener">
              <GitBranch data-icon-inline />
              GitHub
            </a>
          </Button>

          <Button variant="ghost" size="sm" asChild className="hidden sm:inline-flex">
            <a href={COFFEE_URL} target="_blank" rel="noopener">
              <Coffee data-icon-inline />
              Buy me a coffee
            </a>
          </Button>

          <Sheet open={open} onOpenChange={setOpen}>
            <SheetTrigger asChild>
              <Button
                variant="ghost"
                size="icon"
                className="md:hidden"
                aria-label="Open navigation"
              >
                <Menu />
              </Button>
            </SheetTrigger>
            <SheetContent side="right" className="w-72">
              <SheetHeader>
                <SheetTitle>
                  <span className="flex items-center gap-2">
                    <Image
                      src={`${basePath}/logo.webp`}
                      alt=""
                      width={22}
                      height={22}
                      className="rounded-md"
                    />
                    PrintDrop
                  </span>
                </SheetTitle>
              </SheetHeader>
              <nav className="flex flex-col gap-1 px-4" aria-label="Mobile navigation">
                {NAV_LINKS.map((link) => (
                  <SheetClose asChild key={link.href}>
                    <Link
                      href={link.href}
                      className="rounded-md px-3 py-2 text-sm text-muted-foreground transition-colors hover:bg-accent hover:text-foreground"
                    >
                      {link.label}
                    </Link>
                  </SheetClose>
                ))}
              </nav>
              <div className="mt-auto flex flex-col gap-2 p-4">
                <Button variant="outline" asChild className="w-full">
                  <a href={GITHUB_URL} target="_blank" rel="noopener">
                    <GitBranch />
                    View on GitHub
                  </a>
                </Button>
                <Button variant="ghost" asChild className="w-full">
                  <a href={COFFEE_URL} target="_blank" rel="noopener">
                    <Coffee />
                    Buy me a coffee
                  </a>
                </Button>
              </div>
            </SheetContent>
          </Sheet>
        </div>
      </div>
    </header>
  )
}
