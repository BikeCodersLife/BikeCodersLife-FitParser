// @ts-check
// Note: type annotations allow type checking and IDEs autocompletion

const lightCodeTheme = require('prism-react-renderer').themes.github;
const darkCodeTheme = require('prism-react-renderer').themes.dracula;

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'FitParser Documentation',
  tagline: 'High-Performance C++ FIT File Parser - 210x Faster',
  favicon: 'img/favicon.ico',

  // Set the production url of your site here
  url: 'https://bikecoderslife.github.io',
  // Set the /<baseUrl>/ pathname under which your site is served
  baseUrl: '/FitParser/',

  // GitHub pages deployment config.
  organizationName: 'BikeCodersLife',
  projectName: 'FitParser',

  onBrokenLinks: 'warn',

  // Internationalization
  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  // Mermaid diagram support
  markdown: {
    mermaid: true,
    hooks: {
      onBrokenMarkdownLinks: 'warn',
    },
  },
  themes: ['@docusaurus/theme-mermaid'],

  // Client modules for enhanced functionality
  clientModules: [
    require.resolve('./src/theme/mermaidZoom.js'),
  ],

  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          sidebarPath: require.resolve('./sidebars.js'),
          editUrl: 'https://github.com/BikeCodersLife/FitParser/tree/main/docs-site/',
          routeBasePath: '/',
        },
        blog: false,
        theme: {
          customCss: require.resolve('./src/css/custom.css'),
        },
      }),
    ],
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      // Social card
      image: 'img/fitparser-social-card.jpg',
      navbar: {
        title: 'FitParser',
        logo: {
          alt: 'FitParser Logo',
          src: 'img/logo.svg',
        },
        items: [
          {
            type: 'docSidebar',
            sidebarId: 'docsSidebar',
            position: 'left',
            label: 'Documentation',
          },
          {
            href: 'https://docs.velogrid.com',
            label: 'VeloGrid Docs',
            position: 'right',
          },
          {
            href: 'https://github.com/BikeCodersLife/FitParser',
            label: 'GitHub',
            position: 'right',
          },
        ],
      },
      footer: {
        style: 'dark',
        links: [
          {
            title: 'Docs',
            items: [
              {
                label: 'Getting Started',
                to: '/getting-started/installation',
              },
              {
                label: 'Performance',
                to: '/performance/benchmarks',
              },
              {
                label: 'Security',
                to: '/security/security-layers',
              },
            ],
          },
          {
            title: 'BikeCoders Ecosystem',
            items: [
              {
                label: 'VeloGrid Platform',
                href: 'https://docs.velogrid.com',
              },
              {
                label: 'ScalewayJobRunner',
                href: 'https://bikecoderslife.github.io/ScalewayJobRunner/',
              },
              {
                label: 'GpxParser',
                href: 'https://bikecoderslife.github.io/GpxParser/',
              },
            ],
          },
          {
            title: 'More',
            items: [
              {
                label: 'GitHub',
                href: 'https://github.com/BikeCodersLife/FitParser',
              },
              {
                label: 'BikeCoders',
                href: 'https://bikecoders.life',
              },
            ],
          },
        ],
        copyright: `Copyright © ${new Date().getFullYear()} BikeCodersLife. Built with Docusaurus.`,
      },
      prism: {
        theme: lightCodeTheme,
        darkTheme: darkCodeTheme,
        additionalLanguages: ['cpp', 'bash', 'json', 'cmake'],
      },
      // Mermaid theme configuration
      mermaid: {
        theme: {light: 'default', dark: 'dark'},
        options: {
          maxTextSize: 50000,
        },
      },
    }),
};

module.exports = config;
