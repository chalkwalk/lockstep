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
