import { themes as prismThemes } from 'prism-react-renderer';
import type { Config } from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'Lockstep',
  tagline: 'A step sequencer you play, not configure',

  future: {
    v4: true,
  },

  favicon: 'img/favicon.svg',

  url: 'https://lockstep.chalkwalkmusic.com',
  baseUrl: '/',

  organizationName: 'chalkwalk',
  projectName: 'lockstep',

  // Throw, so a stale cross-reference between guide pages fails the deploy
  // rather than shipping as a dead link.
  onBrokenLinks: 'throw',

  // Parse .md as CommonMark, not MDX.
  //
  // Docusaurus 3 treats .md as MDX by default, which makes every `<` a
  // potential JSX tag. This documentation is migrated prose from README.md and
  // is full of angle-bracket placeholders -- `<reason>`, `<name>`, `Func+<key>`
  // -- plus comparison operators in tables. MDX rejects them, and escaping
  // several dozen of them would be a permanent tax on writing ordinary English
  // here. 'detect' keeps MDX available for any .mdx file that wants it.
  markdown: {
    format: 'detect',
  },

  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  presets: [
    [
      'classic',
      {
        docs: {
          sidebarPath: './sidebars.ts',
          // Docs under /docs, with a landing page at the root -- the same
          // shape as antiphon, arps-euclidya and star-canopy.
          //
          // This briefly served docs at the root instead, because the hero
          // wants a logo and a screenshot and inventing artwork is worse than
          // having none. Both turned out to exist already: the screenshot is a
          // blessed scene golden from the test suite, so it cannot drift from
          // what the app draws without a test failing, and the mark is drawn
          // from the surface's own step grid and accent colour.
          routeBasePath: 'docs',
          editUrl: 'https://github.com/chalkwalk/lockstep/tree/main/website/',
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      } satisfies Preset.Options,
    ],
  ],

  themeConfig: {
    colorMode: {
      respectPrefersColorScheme: true,
    },
    image: 'img/social-card.png',
    navbar: {
      title: 'Lockstep',
      // Decorative: the title beside it already carries the name, so alt text
      // here would only make a screen reader say "Lockstep" twice.
      // The simplified mark, not the full one: the navbar renders it at about
      // 32px, where the detailed mark's gear teeth alias into a blur.
      logo: { alt: '', src: 'img/favicon.svg' },
      items: [
        {
          type: 'docSidebar',
          sidebarId: 'docsSidebar',
          position: 'left',
          label: 'Documentation',
        },
        {
          href: 'https://github.com/chalkwalk/lockstep',
          label: 'GitHub',
          position: 'right',
        },
      ],
    },
    footer: {
      style: 'dark',
      links: [
        {
          title: 'Documentation',
          items: [
            { label: 'What Lockstep is', to: '/docs/' },
            { label: 'Installing', to: '/docs/installing' },
            { label: 'Troubleshooting', to: '/docs/troubleshooting' },
          ],
        },
        {
          title: 'Project',
          items: [
            { label: 'GitHub', href: 'https://github.com/chalkwalk/lockstep' },
            {
              label: 'Contributing',
              href: 'https://github.com/chalkwalk/lockstep/blob/main/CONTRIBUTING.md',
            },
            {
              label: 'Wiki',
              href: 'https://github.com/chalkwalk/lockstep/wiki',
            },
          ],
        },
      ],
      copyright: `Lockstep is free software under the GPLv3. Built ${new Date().getFullYear()}.`,
    },
    prism: {
      theme: prismThemes.github,
      darkTheme: prismThemes.dracula,
    },
  } satisfies Preset.ThemeConfig,
};

export default config;
