document.addEventListener("DOMContentLoaded", () => {
  if (typeof gsap === "undefined") return;

  const prefersReducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  if (typeof ScrollTrigger !== "undefined") {
    gsap.registerPlugin(ScrollTrigger);
  }

  if (prefersReducedMotion) {
    gsap.set([
      ".hero-copy > *",
      ".hero-logo-wrap",
      ".hero-badge",
      ".feature-card",
      ".stat-card",
      ".security-copy > *",
      ".security-screen",
      ".team-card",
      ".support-box",
      ".signup-copy > *",
      ".signup-form-card",
      ".secondary-signup-inner > *"
    ], {
      clearProps: "all",
      opacity: 1
    });
    return;
  }

  const heroTimeline = gsap.timeline({ defaults: { ease: "power4.out" } });

  heroTimeline
    .from(".site-header", {
      y: -24,
      opacity: 0,
      duration: 0.8
    })
    .from(".eyebrow", {
      y: 24,
      opacity: 0,
      duration: 0.7
    }, "-=0.35")
    .from(".hero h1", {
      y: 70,
      opacity: 0,
      duration: 1.1
    }, "-=0.35")
    .from(".hero-sub", {
      y: 40,
      opacity: 0,
      duration: 0.9
    }, "-=0.75")
    .from(".hero-actions .btn", {
      y: 24,
      opacity: 0,
      duration: 0.7,
      stagger: 0.12
    }, "-=0.55")
    .from(".hero-logo-wrap", {
      scale: 0.92,
      y: 30,
      opacity: 0,
      duration: 1
    }, "-=1")
    .from(".hero-badge", {
      y: 20,
      opacity: 0,
      duration: 0.8
    }, "-=0.55")
    .from(".hero-scroll", {
      y: 12,
      opacity: 0,
      duration: 0.6
    }, "-=0.4");

  if (typeof ScrollTrigger !== "undefined") {
    gsap.to(".hero-video video", {
      yPercent: 10,
      ease: "none",
      scrollTrigger: {
        trigger: ".hero",
        start: "top top",
        end: "bottom top",
        scrub: true
      }
    });

    gsap.to(".hero-logo-wrap", {
      yPercent: -8,
      ease: "none",
      scrollTrigger: {
        trigger: ".hero",
        start: "top top",
        end: "bottom top",
        scrub: true
      }
    });

    const revealGroups = [
      {
        trigger: ".signup-band",
        targets: [".signup-copy > *", ".signup-form-card"]
      },
      {
        trigger: "#features",
        targets: [".section-head > *", ".feature-card"]
      },
      {
        trigger: "#security",
        targets: [".security-copy > *", ".security-screen"]
      },
      {
        trigger: ".secondary-signup",
        targets: [".secondary-signup-inner > *"]
      },
      {
        trigger: "#team",
        targets: [".team-section .section-head > *", ".team-card", ".support-box"]
      },
      {
        trigger: ".site-footer",
        targets: [".footer-inner > *"]
      }
    ];

    revealGroups.forEach((group) => {
      gsap.from(group.targets, {
        y: 60,
        opacity: 0,
        duration: 0.9,
        stagger: 0.12,
        ease: "power3.out",
        scrollTrigger: {
          trigger: group.trigger,
          start: "top 82%",
          once: true
        }
      });
    });

    gsap.from(".stat-card", {
      y: 50,
      opacity: 0,
      duration: 0.8,
      stagger: 0.12,
      ease: "power3.out",
      scrollTrigger: {
        trigger: ".stats-section",
        start: "top 82%",
        once: true
      }
    });

    const statNumbers = document.querySelectorAll(".stat-num");

    statNumbers.forEach((stat) => {
      const rawValue = stat.dataset.value || stat.textContent.trim();
      const numericValue = parseInt(rawValue, 10);

      if (Number.isNaN(numericValue)) return;

      const suffix = rawValue.toString().replace(String(numericValue), "") || "";
      const counter = { value: 0 };

      gsap.to(counter, {
        value: numericValue,
        duration: 2,
        ease: "power2.out",
        scrollTrigger: {
          trigger: stat,
          start: "top 88%",
          once: true
        },
        onUpdate: () => {
          stat.textContent = Math.round(counter.value) + suffix;
        }
      });
    });

    gsap.from(".security-list li", {
      x: -26,
      opacity: 0,
      duration: 0.7,
      stagger: 0.12,
      ease: "power3.out",
      scrollTrigger: {
        trigger: ".security-list",
        start: "top 85%",
        once: true
      }
    });

    gsap.from(".feature-card", {
      scale: 0.96,
      transformOrigin: "center center",
      duration: 0.9,
      stagger: 0.1,
      ease: "power3.out",
      scrollTrigger: {
        trigger: ".features-grid",
        start: "top 80%",
        once: true
      }
    });
  }

  const magneticButtons = document.querySelectorAll(".btn, .nav-cta");

  magneticButtons.forEach((button) => {
    button.addEventListener("mousemove", (event) => {
      const rect = button.getBoundingClientRect();
      const x = event.clientX - rect.left;
      const y = event.clientY - rect.top;

      const moveX = (x - rect.width / 2) * 0.06;
      const moveY = (y - rect.height / 2) * 0.10;

      gsap.to(button, {
        x: moveX,
        y: moveY,
        duration: 0.25,
        ease: "power2.out"
      });
    });

    button.addEventListener("mouseleave", () => {
      gsap.to(button, {
        x: 0,
        y: 0,
        duration: 0.35,
        ease: "power3.out"
      });
    });
  });

  const cards = document.querySelectorAll(".feature-card, .team-card, .stat-card");

  cards.forEach((card) => {
    card.addEventListener("mouseenter", () => {
      gsap.to(card, {
        y: -6,
        duration: 0.25,
        ease: "power2.out"
      });
    });

    card.addEventListener("mouseleave", () => {
      gsap.to(card, {
        y: 0,
        duration: 0.3,
        ease: "power2.out"
      });
    });
  });
});