import { themes as prismThemes } from 'prism-react-renderer';
import type { Config } from '@docusaurus/types';
import type * as Preset from '@docusaurus/preset-classic';

const config: Config = {
  title: 'Lockstep',
  tagline: 'A step sequencer you play, not configure',

  future: {
    v4: true,
  },

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
          // Docs at the root, unlike the sibling sites, which serve them under
          // /docs behind a React landing page. That landing page wants a logo,
          // a social card and a favicon, and shipping another project's
          // artwork as Lockstep's would be worse than having none. So the
          // domain opens straight onto the documentation until there is real
          // artwork to put in front of it.
          routeBasePath: '/',
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
    navbar: {
      title: 'Lockstep',
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
            { label: 'What Lockstep is', to: '/' },
            { label: 'Installing', to: '/installing' },
            { label: 'Troubleshooting', to: '/troubleshooting' },
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
