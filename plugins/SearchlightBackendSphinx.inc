<?php

define('SEARCHLIGHT_SPHINX_FILTER_MAX', 100000000000);
define('SEARCHLIGHT_SPHINX_FILTER_MIN', 0);

/**
 * Provides integration with Sphinx.
 */
class SearchlightBackendSphinx extends SearchlightBackend {
  /**
   * Override of settingsDefault().
   */
  function settingsDefault() {
    return array(
      'port' => 9312,
      'delta_ttl' => 0,
      'sql_sock' => '',
      'index' => array(
        'morphology' => 'stem_en',
        'charset_table' => NULL,
        'ngram_len' => NULL,
        'ngram_chars' => NULL,
      ),
    );
  }

  /**
   * Override of settingsForm().
   */
  function settingsForm() {
    $form = array();
    $form['sql_sock'] = array(
      '#title' => t('DB sock'),
      '#type' => 'textfield',
      '#default_value' => $this->settings['sql_sock'],
      '#description' => t('The DB sock file to use. Example: <code>/var/lib/mysql/mysql.sock</code>. If left empty, Sphinx will try to autodetect its location.'),
      '#size' => 30,
    );
    $form['port'] = array(
      '#title' => t('searchd port'),
      '#type' => 'textfield',
      '#default_value' => $this->settings['port'],
      '#size' => 6,
    );
    $form['delta_ttl'] = array(
      '#title' => t('Delta index TTL'),
      '#description' => t('In seconds. Set to "0" to re-index on every run.'),
      '#type' => 'textfield',
      '#default_value' => $this->settings['delta_ttl'],
      '#size' => 6,
    );
    $form['index'] = array(
      '#type' => 'fieldset',
      '#title' => t('Index settings'),
      '#description' => t('Advanced configuration options for Sphinx. Use these options to enable CJK or other character set handling for Sphinx. Otherwise, you can leave these blank.'),
    );
    $form['index']['morphology'] = array(
      '#title' => t('Morphology'),
      '#description' => t('Morphology processors to apply. See !link', array(
        '!link' => l(t('Sphinx: morphology'), 'http://www.sphinxsearch.com/docs/current.html#conf-morphology'),
      )),
      '#type' => 'textfield',
      '#default_value' => $this->settings['index']['morphology'],
    );
    $form['index']['charset_table'] = array(
      '#title' => t('Character set table'),
      '#description' => t('Character set table. See !link and !charset for a variety of options', array(
        '!link' => l(t('Sphinx: charset_table'), 'http://www.sphinxsearch.com/docs/current.html#conf-charset-table'),
        '!charset' => l(t('Unicode character set tables'), 'http://sphinxsearch.com/wiki/doku.php?id=charset_tables'),
      )),
      '#type' => 'textfield',
      '#default_value' => $this->settings['index']['charset_table'],
    );
    $form['index']['ngram_len'] = array(
      '#title' => t('N-gram length'),
      '#description' => t('Set n-gram length to 1 for basic CJK support. See !link', array(
        '!link' => l(t('Sphinx: ngram_len'), 'http://www.sphinxsearch.com/docs/current.html#ngram_len'),
      )),
      '#type' => 'textfield',
      '#default_value' => $this->settings['index']['ngram_len'],
    );
    $form['index']['ngram_chars'] = array(
      '#title' => t('N-gram characters list'),
      '#description' => t('N-gram characters list for basic CJK support. See !link and !charset for CJK support', array(
        '!link' => l(t('Sphinx: ngram_chars'), 'http://www.sphinxsearch.com/docs/current.html#ngram_chars'),
        '!charset' => l(t('CJK n-gram characters'), 'http://sphinxsearch.com/wiki/doku.php?id=charset_tables#cjk_ngram_characters'),
      )),
      '#type' => 'textfield',
      '#default_value' => $this->settings['index']['ngram_chars'],
    );
    return $form;
  }

  /**
   * Override of viewsOptionDefinition().
   */
  function viewsOptionDefinition(&$handler) {
    $this->sphinxIncludeClient();
    $options = parent::viewsOptionDefinition($handler);
    $options['matchMode'] = array('default' => SPH_MATCH_ALL);
    return $options;
  }

  /**
   * Override of viewsOptionsForm().
   */
  function viewsOptionsForm(&$form, &$form_state, &$handler) {
    $this->sphinxIncludeClient();
    parent::viewsOptionsForm($form, $form_state, $handler);
    $form['matchMode'] = array(
      '#title' => t('Search mode'),
      '#type' => 'select',
      '#options' => array(
        SPH_MATCH_ALL => t('Match all query words'),
        SPH_MATCH_ANY => t('Match any of the query words'),
        SPH_MATCH_PHRASE => t('Match the exact phrase'),
        SPH_MATCH_EXTENDED2 => t('Matches a Sphinx extended query'),
      ),
      '#default_value' => $handler->options['matchMode'],
    );
  }

  /**
   * Override of initClient().
   */
  function initClient($datasource) {
    $this->sphinxIncludeClient();
    $client = new SphinxClient();
    $client->SetServer('localhost', (int) $this->settings['port']);
    return $client;
  }

  /**
   * Override of executeQuery().
   */
  function executeQuery(&$client, $datasource, $query = '') {
    $result = $client->Query($query, $this->sphinxGetIndex($datasource));
    if (!empty($result['matches'])) {
      return array('result' => array_keys($result['matches']), 'total' => $result['total'], 'raw' => $result);
    }
    return FALSE;
  }

  /**
   * Override of facetBuild().
   */
  function facetBuild(&$client, $datasource, $query = '', $facets) {
    $client = clone $client;
    foreach ($facets as $facet) {
      $field = $datasource->fields[$facet['field']];
      $limit = $facet['limit'];
      switch ($field['datatype']) {
        case 'timestamp':
        case 'date':
          $groupby = array('day' => SPH_GROUPBY_DAY, 'month' => SPH_GROUPBY_MONTH, 'year' => SPH_GROUPBY_YEAR);
          $granularity = !empty($facet['granularity']) ? $facet['granularity'] : 'month';
          $client->SetGroupBy($field['name'], $groupby[$granularity], '@group desc');
          $client->SetArrayResult(FALSE);
          break;
        default:
          $client->SetGroupBy($field['name'], SPH_GROUPBY_ATTR, '@count desc');
          $client->SetArrayResult($field['usage'] === 'multivalue');
          break;
      }

      // Set result limit.
      if (!empty($limit)) {
        $client->SetLimits(0, (int) $limit);
      }
      $client->AddQuery($query, $this->sphinxGetIndex($datasource));
    }

    // Execute queries.
    $results = $client->RunQueries();

    // Build facet results.
    $built = array();
    foreach (array_values($facets) as $num => $facet) {
      if (!empty($results[$num]['matches'])) {
        $field = $datasource->fields[$facet['field']];
        foreach (array_keys($results[$num]['matches']) as $id) {
          $attr = $results[$num]['matches'][$id]['attrs'];
          if (isset($attr['@groupby'], $attr['@count'])) {
            switch ($field['datatype']) {
              case 'text':
                $id = $this->sphinxGetOrdinal($datasource, $field['name'], $attr['@groupby']);
                break;
              case 'timestamp':
              case 'date':
                $id = $this->sphinxGetTimestamp($attr['@groupby'], !empty($facet['granularity']) ? $facet['granularity'] : 'month');
                break;
              default:
                $id = $attr['@groupby'];
                break;
            }
            $built[$facet['field']][$id] = array('id' => $id, 'count' => $attr['@count']);
          }
        }
      }
    }
    return $built;
  }

  /**
   * Override of invalidateIndex().
   * Since Sphinx indexes can't be fully deleted from within Drupal, we mark
   * all items as deleted instead.
   */
  function invalidateIndex($datasource) {
    variable_del('searchlight_sphinx_last');

    $delete = array();
    $result = db_select($datasource->base_table, 'b')
      ->fields('b', array($datasource->base_field))
      ->execute();
    foreach ($result as $row) {
      $delete[$row->{$datasource->base_field}] = array(1);
    }

    $client = $this->initClient($datasource);
    $client->UpdateAttributes($datasource->id, array('searchlight_deleted'), $delete);
    $client->UpdateAttributes($datasource->id .'_delta', array('searchlight_deleted'), $delete);
  }

  /**
   * Override of setFilter().
   */
  function setFilter(&$client, $datasource, $filters) {
    // Hardwired delete filter. See sphinxDatasourceConf() for how this is
    // added to the sphinx index.
    $filters['searchlight_deleted'] = array(
      'field' => 'searchlight_deleted',
      'operator' => '=',
      'args' => array(0),
    );
    if (!empty($filters)) {
      // Iterate through once to separate out range filters.
      $range_filters = array();
      foreach ($filters as $key => $params) {
        $field    = $params['field'];
        $operator = $params['operator'];
        $args     = $params['args'];
        if (in_array($operator, array('<', '>', '>=', '<='))) {
          unset($filters[$key]);
          switch ($operator) {
            case '>':
            case '>=':
              $range_filters[$field]['field'] = $field;
              $range_filters[$field]['max'] = isset($range_filters[$field]['max']) ? $range_filters[$field]['max'] : SEARCHLIGHT_SPHINX_FILTER_MAX;
              $range_filters[$field]['min'] = eval("return {$args[0]};");
              $range_filters[$field]['exclude'] = $operator === '>' ? TRUE : FALSE;
              break;
            case '<':
            case '<=':
              $range_filters[$field]['field'] = $field;
              $range_filters[$field]['max'] = eval("return {$args[0]};");
              $range_filters[$field]['min'] = isset($range_filters[$field]['min']) ? $range_filters[$field]['min'] : SEARCHLIGHT_SPHINX_FILTER_MIN;
              $range_filters[$field]['exclude'] = $operator === '<' ? TRUE : FALSE;
              break;
            default:
              break;
          }
        }
      }
      // Now set range filters.
      foreach ($range_filters as $filter) {
        // The exclude option on range filters appears to be broken in
        // Sphinx 0.9.9. Update this code to support either operator once the
        // problem is fixed upstream.
        // $client->SetFilterRange($filter['field'], $filter['min'], $filter['max'], $filter['exclude']);
        $client->SetFilterRange($filter['field'], $filter['min'], $filter['max'], FALSE);
      }
      // Equality & set filters filters.
      foreach ($filters as $params) {
        $field    = $params['field'];
        $operator = $params['operator'];
        $args     = $params['args'];
        switch ($operator) {
          case '=':
            $this->sphinxSetFilter($client, $datasource, $field, $args, FALSE);
            break;
          case 'IN':
            $this->sphinxSetFilter($client, $datasource, $field, $args, FALSE);
            break;
          case '<>':
          case 'NOT IN':
            $this->sphinxSetFilter($client, $datasource, $field, $args, TRUE);
            break;
        }
      }
    }
  }

  /**
   * Override of setOptions().
   */
  function setOptions(&$client, $datasource, $options) {
    if (isset($options['matchMode'])) {
      $client->SetMatchMode($options['matchMode']);
    }
  }

  /**
   * Override of setSort().
   */
  function setSort(&$client, $datasource, $sorts) {
    if (!empty($sorts)) {
      $sphinx_sorts = array();
      foreach ($sorts as $sort) {
        if ($sort['field'] === 'searchlight_weight') {
          $sphinx_sorts[] = "@weight {$sort['direction']}";
        }
        else {
          $sphinx_sorts[] = "{$sort['field']} {$sort['direction']}";
        }
      }
      $client->setSortMode(SPH_SORT_EXTENDED, implode(', ', $sphinx_sorts));
    }
  }

  /**
   * Override of setPager().
   */
  function setPager(&$client, $offset, $limit) {
    $limit = !empty($limit) ? $limit : 1000;
    if (!empty($offset) || !empty($limit)) {
      $client->SetLimits((int) $offset, (int) $limit);
    }
  }

  /**
   * Override of setNodeAccess().
   * For Sphinx API usage of filter ORs, see:
   * - http://www.sphinxsearch.com/faq.html#filter-or
   * - http://www.sphinxsearch.com/forum/view.html?id=3440
   */
  function setNodeAccess(&$client, $grants) {
    if (!empty($grants)) {
      $grants_sql = array();
      foreach ($grants as $realm => $gids) {
        foreach ($gids as $gid) {
          $grants_sql[] = "IF(IN(node_access_gid_{$realm}, {$gid}), 1, 0)";
        }
      }
      if (count($grants_sql)) {
        $grants_sql = implode('+', $grants_sql);
        $client->SetSelect("*, ({$grants_sql}) AS sphinx_node_access");
        $client->SetFilterRange('sphinx_node_access', 1, 1000);
      }
    }
  }


  /**
   * Return a directory where the configuration files and indexes are stored.
   *
   * This can be overridden by specifying the '--sl-sphinx-conf-path' command line option to drush.
   */
  function drushGetConfigPath() {
    return drush_get_option('sl-sphinx-conf-path', dirname(drush_locate_root()) . '/sphinx');
  }

  /**
   * Return the file name of the generated configuration file.
   */
  function drushGetConfigFile() {
    return $this->drushGetConfigPath() . '/sphinx.merged.conf';
  }

  /**
   * Generate a merged configuration file.
   */
  function drushGenerateMergedConfig() {
    $conf_path = $this->drushGetConfigPath();
    $conf_file = $this->drushGetConfigFile();

    if (file_prepare_directory($conf_path, TRUE)) {
      $files = array();

      // rerieve list of possible configuration file matches.
      $files[] = $conf_path . '/sphinx.conf';

      $matches = drush_scan_directory($conf_path . '/index.d/', '/^.*\.conf$/', array('.', '..'), 0, false, 'name');

      foreach ($matches as $name => $file) {
        $files[] = $file->filename;
      }

      $string = '';
      if (sizeof($files)) {
        foreach ($files as $filename) {
          if (file_exists($filename) && is_readable($filename)) {
            // concatenate contents of valid configuration file.
            $string .= "\n" . file_get_contents($filename);
          }
        }
      }

      // load the previous file
      $old_config = (file_exists($conf_file)) ? file_get_contents($conf_file) : '';

      if ($old_config != $string) {
        file_put_contents($conf_file, $string);
        return drush_log(dt("Regenerated the Sphinx configuration file"), 'success');
      }
      else {
        drush_log(dt("Sphinx configuration file is up to date"), 'success');
      }
    }
    return false;
  }

  /**
   * Override of drushSearchd().
   */
  function drushSearchd($command = 'start') {
    $file_path = $this->drushGetConfigPath() . '/log';

    $lock_file = "{$file_path}/searchd.lock";
    if (file_prepare_directory($file_path, TRUE)) {
      $lock = file_exists($lock_file) ? file_get_contents($lock_file) : 0;

      if ((int) $lock == 1) {
        return drush_log('Search demon is already being restarted', 'error');
      }

      file_put_contents($lock_file, '1');
      if (file_prepare_directory($file_path, TRUE)) {
        $pid_file = "{$file_path}/searchd.pid";
        $pid = file_exists($pid_file) ? file_get_contents($pid_file) : '';

        if ((int) $pid) {
          // stop the daemon first
          drush_op('drush_shell_exec', 'searchd --nodetach --stop -c ' . $this->drushGetConfigFile());
          drush_log('Search daemon stopped.', 'success');
        }

        if ($command != 'stop') {
          // start the daemon
          drush_op('drush_shell_exec', 'searchd -c ' . $this->drushGetConfigFile());
          drush_log('Search daemon started.', 'success');
        }
      }
      file_put_contents($lock_file, '0');
      return;
    }
  }

  /**
   * Override of drushIndex().
   */
  function drushIndex() {
    // Create the sphinx directory if it doesn't exist.
    $changed = $this->drushGenerateMergedConfig();
    $file_path = $this->drushGetConfigPath() . '/indexes';
    if (file_prepare_directory($file_path, TRUE)) {
      // Determine if we should update deltas only.
      $delta = $this->settings['delta_ttl'] && (variable_get('searchlight_sphinx_last', 0) + $this->settings['delta_ttl']) > REQUEST_TIME;

      // Build list of this site's indexes.
      $indexes = array();
      $indexes_delta = array();
      foreach (searchlight_get_datasource() as $datasource) {
        $datasource->init();
        $indexes[$datasource->id] = $datasource->id;
        $indexes_delta[$datasource->id] = $datasource->id . '_delta';
      }

      drush_op('drush_shell_exec', 'indexer --rotate --config ' . $this->drushGetConfigFile() . ' ' . implode(' ', $delta ? $indexes_delta : $indexes));
      $output = drush_shell_exec_output();
      foreach ($output as $line) {
        drush_print($line);
      }

      // Take care of entity deletion by updating searchlight_delta to 1.
      // Note that we must update both the main and delta indices.
      foreach (searchlight_get_datasource() as $datasource) {
        $delete = array();
        $result = db_select('searchlight_search', 's')
          ->fields('s')
          ->condition('status', -1)
          ->condition('type', $datasource->base_table)
          ->execute();
        foreach ($result as $row) {
          $delete[$row->id] = array(1);
        }
        if (!empty($update)) {
          $client = $this->initClient($datasource);
          $client->UpdateAttributes($indexes[$datasource->id], array('searchlight_deleted'), $delete);
          $client->UpdateAttributes($indexes_delta[$datasource->id], array('searchlight_deleted'), $delete);

          // Once updates are complete, remove the Drupal-side records.
          db_delete('searchlight_search')
            ->condition('status', -1)
            ->condition('type', $datasource->base_table)
            ->execute();
        }
      }

      // Clear caches so that sphinx id -> facet name mapping is updated.
      cache_clear_all('searchlight', 'cache', TRUE);
      db_update('searchlight_search')
        ->fields(array('status' => 1))
        ->condition('status', 0)
        ->execute();
      variable_set('searchlight_sphinx_last', REQUEST_TIME);
      drush_log('Indexing complete.', 'success');
      if ($changed === TRUE) {
        return $this->drushSearchd();
      }
    }
    else {
      return drush_log("An error ocurred while indexing.", 'error');
    }
  }

  /**
   * Override of drushConf().
   */
  function drushConf() {
    $file_path = $this->drushGetConfigPath();
    $conf_path = $file_path .'/index.d';

    // @TODO: Revisit configuration file generation. Need to handle:
    //
    // 1. If the datasource configuration has changed due to a site-driven
    //    event (eg. edit form), we can use the 'searchlight_config_changed'
    //    variable as a sign that files need rewriting.
    // 2. If the site has been migrated (eg. through Aegir) and the database
    //    name has changed, we need to rewrite config files. This case is NOT
    //    handled by the variable.
    // 3. If the site's code has changed and datasources are in the default
    //    state, we need to rewrite config files. This case is NOT handled by
    //    the variable.
    //
    // if (!variable_get('searchlight_config_changed', TRUE) && file_check_directory($conf_path, TRUE)) {
    //   return drush_log('Sphinx configuration not changed', 'success');
    // }
    // else {
    //   variable_set('searchlight_config_changed', FALSE);
    // }

    if (file_prepare_directory($file_path, TRUE) && drush_get_option('sl-sphinx-base-conf', TRUE)) {
      $this->drushBaseConf();
    }

    if (file_prepare_directory($conf_path, TRUE)) {
      // Collect configuration arrays for each datasource.
      foreach (searchlight_datasource_load(NULL, TRUE) as $datasource) {
        $source = $this->sphinxDatasourceConf($datasource);

        // Add delta index.
        if (!empty($this->settings['delta_ttl'])) {
          $source = $this->sphinxDatasourceConf($source, TRUE);
        }

        $conf_file = "{$conf_path}/{$source['conf']['id']}.conf";

        // Generate configuration file from datasource.
        $sphinx_conf = theme('searchlight_sphinx_index_conf', array('datasource' => $source));
        $existing = file_exists($conf_file) ? file_get_contents($conf_file) : '';
        if ($sphinx_conf !== $existing) {
          file_put_contents($conf_file, $sphinx_conf);
          drush_log("{$conf_file} was written successfully.", 'success');
        }
      }
      return drush_log("Sphinx configuration files were written", 'success');
    }
    return drush_log("Sphinx configuration files could not be written.", 'error');
  }

  /**
   * Generate the base configuration for all sphinx instances.
   */
  function drushBaseConf() {
    $file_path = $this->drushGetConfigPath();
    $conf_file = "{$file_path}/sphinx.conf";
    if (file_prepare_directory($file_path, TRUE)) {
      $sphinx_conf = theme('searchlight_sphinx_conf', array('searchd' => $this->sphinxSearchdConf()));
      if ($sphinx_conf) {
        $existing = file_exists($conf_file) ? file_get_contents($conf_file) : '';
        if ($sphinx_conf === $existing) {
          return drush_log("{$conf_file} is unchanged.", 'success');
        }
        else {
          file_put_contents($conf_file, $sphinx_conf);
          return drush_log("{$conf_file} was written successfully.", 'success');
        }
      }
    }
  }

  /**
   * Execute functionality on drush cron run.
   */
  function drushCron() {
    $this->drushConf();
    $this->drushIndex();
  }


  /**
   * When a new site is installed via Aegir.
   */
  function drushAegirInstall() {
    $conf_path = drush_set_option('sl-sphinx-conf-path', drush_get_option('config_path', '/var/aegir') . '/sphinx', 'site');

    $datasources = searchlight_datasource_load(NULL, TRUE);
    $this->drushConf();
    $this->drushIndex();

    // Drush backend invoke is not usable before drushrc.php is written.
    // drush_backend_invoke('searchlight-conf', array('sl-sphinx-conf-path' => $conf_path));
    // drush_backend_invoke('searchlight-index', array('sl-sphinx-conf-path' => $conf_path));
  }

  /**
   * When a site is migrated via Aegir.
   */
  function drushAegirDeploy() {
    $this->drushAegirInstall();
  }

  /**
   * Methods specific to the Sphinx backend ===================================
   */

  /**
   * Wrapper of $client->SetFilter().
   * Convert any string values to ordinals before passing through.
   */
  function sphinxSetFilter(&$client, $datasource, $attribute, $values, $exclude = FALSE) {
    $ordinals = array();
    foreach ((array) $values as $arg) {
      $arg = trim($arg);
      if (is_numeric($arg)) {
        $ordinals[] = $arg;
      }
      else {
        $ordinal = $this->sphinxGetOrdinal($datasource, $attribute, $arg);
        if (is_numeric($ordinal)) {
          $ordinals[] = $ordinal;
        }
      }
    }
    return !empty($ordinals) ? $client->SetFilter($attribute, $ordinals, $exclude) : FALSE;
  }

  /**
   * Generate an array representing the conf and index settings for a datasource.
   */
  function sphinxDatasourceConf($datasource, $delta = FALSE) {
    $datasource->init();
    $datasource_id = $delta ? $datasource->id . '_delta' : $datasource->id;

    $conf = array(
      'conf' => array(),
      'index' => array(),
    );

    // Configuration options.
    $conf['conf']['id'] = $datasource_id;

    // Use mysql stored database credentials.
    if ($creds = drush_get_context('DRUSH_DB_CREDENTIALS')) {
      $conf['conf']['type'] = $creds['driver'] === 'mysqli' ? 'mysql' : $creds['driver'];
      $conf['conf']['sql_user'] = $creds['user'];
      $conf['conf']['sql_pass'] = $creds['pass'];
      $conf['conf']['sql_host'] = $creds['host'];
      $conf['conf']['sql_db']   = $creds['name'];
      $conf['conf']['sql_port'] = isset($creds['port']) ? $creds['port'] : '3306';

    }
    // Check for optional sql_sock option.
    $sock = trim($this->settings['sql_sock']);
    if (!empty($sock)) {
      $conf['conf']['sql_sock'] = $sock;
    }

    // Index information
    $conf['index']['path'] = $this->drushGetConfigPath() . '/indexes/' . $datasource_id;
    $conf['index']['docinfo'] = 'extern';
    $conf['index']['mlock'] = 0;

    // Morphology settings.
    if (!empty($this->settings['index']['morphology'])) {
      $conf['index']['morphology'] = $this->settings['index']['morphology'];
    }
    // Charactsets, CJK handling, etc.
    if (!empty($this->settings['index']['charset_table'])) {
      $conf['index']['charset_table'] = $this->settings['index']['charset_table'];
    }
    if (!empty($this->settings['index']['ngram_len'])) {
      $conf['index']['ngram_len'] = $this->settings['index']['ngram_len'];
    }
    if (!empty($this->settings['index']['ngram_chars'])) {
      $conf['index']['ngram_chars'] = $this->settings['index']['ngram_chars'];
    }

    // Assume UTF-8, character stripping for now.
    $conf['index']['charset_type'] = 'utf-8';
    $conf['index']['html_strip'] = 0;

    // Build the query.
    $datasource->init();
    $datasource->view->build();
    if ($view = $datasource->view) {
      // Force utf8 when indexing.
      $conf['conf']['sql_query_pre'] = "SET NAMES utf8";

      $sql_query = clone $view->query;

      $sql_query->add_where(0, "{$view->base_table}.{$view->base_field}" .  ' BETWEEN $start AND $end ', array(), 'formula');
      $sql_query->add_field(NULL, '0', 'searchlight_deleted');
      if ($delta) {
        $this->queryFresh($sql_query);
      }
      $conf['conf']['sql_query'] = $this->sphinxWriteSQL($sql_query);
      $conf['conf']['sql_query'] = str_replace("\n", " \\\n", trim($conf['conf']['sql_query']));

      // Build the info query.
      $sql_query_info = clone $view->query;
      $sql_query_info->add_where(0, "{$view->base_table}.{$view->base_field}" . ' = $id', array(), 'formula');
      $sql_query_info->add_field(NULL, '0', 'searchlight_deleted');
      if ($delta) {
        $this->queryFresh($sql_query_info);
      }
      $conf['conf']['sql_query_info'] = $this->sphinxWriteSQL($sql_query);
      $conf['conf']['sql_query_info'] = str_replace("\n", " \\\n", trim($conf['conf']['sql_query_info']));

      // Assume serial ids on the base table and step by 1000.
      $conf['conf']['sql_query_range'] = "SELECT MIN({$view->base_field}), MAX({$view->base_field}) FROM {$view->base_table} WHERE {$view->base_field} > 0";
      $conf['conf']['sql_query_range'] = str_replace("\n", " \\\n", trim($conf['conf']['sql_query_range']));
      $conf['conf']['sql_range_step'] = 1000;
      $conf['conf']['sql_ranged_throttle'] = 0;

      // Merge in attributes.
      $sql_attr = array();
      $sphinx_type = array(
        'text' => 'sql_attr_str2ordinal',
        'int' => 'sql_attr_uint',
        'float' => 'sql_attr_float',
        'timestamp' => 'sql_attr_timestamp',
      );

      // Hardwired delete flag.
      $sql_attr[] = "{$sphinx_type['int']} = searchlight_deleted";

      // Generate attribute entries.
      $fields = $datasource->fields;
      $fields = array_merge($fields, $datasource->buildNodeaccess());
      foreach ($fields as $field) {
        if ($field['usage'] === 'attribute' && isset($sphinx_type[$field['datatype']])) {
          $sql_attr[] = "{$sphinx_type[$field['datatype']]} = {$field['name']}";
        }
        // Generate multivalue queries.
        else if ($field['usage'] === 'multivalue' && $mva_view = searchlight_build_view($field['view'])) {
          $query = clone $mva_view->query;
          // Remove any fields that are not the id field or attribute field.
          foreach ($query->fields as $alias => $query_field) {
            if ($query_field['field'] === $mva_view->base_field && $query_field['table'] === $mva_view->base_table) {
              continue;
            }
            if ($query_field['field'] === $field['field'] && $query_field['table'] === $field['table']) {
              continue;
            }
            unset($query->fields[$alias]);
          }
          if ($delta) {
            $this->queryFresh($query);
          }
          $mva_query = $this->sphinxWriteSQL($query);
          $mva_query = str_replace("\n", " \\\n", trim($mva_query));
          $sql_attr[] = "sql_attr_multi = uint {$field['name']} from query; {$mva_query}";
        }
      }

      $sql_attr = array_unique($sql_attr);
      $conf['conf']['sql_attr'] = implode(" \n", $sql_attr);
    }

    return $conf;
  }

  /**
   * Get the Sphinx searchd settings.
   */
  function sphinxSearchdConf() {
    $searchd = array();
    $searchd['log'] = $this->drushGetConfigPath() . '/log/searchd.log';
    $searchd['query_log'] = $this->drushGetConfigPath() . '/log/query.log';
    $searchd['pid_file'] = $this->drushGetConfigPath() . '/log/searchd.pid';
    return $searchd;
  }

  /**
   * Include the Sphinx PHP API library.
   */
  function sphinxIncludeClient() {
    module_load_include('php', 'searchlight', 'libraries/sphinxapi');
  }

  /**
   * Write a SQL query with fully prefixed tables and replaced arguments.
   */
  function sphinxWriteSQL($query) {
    $connection = Database::getConnection('default');
    $sql = $connection->prepareQuery( (string) $query->query())->queryString;

    // Monstrous hack. Because PDO only allows access to prepared queries and
    // not the full query with placeholders replaced with actual values we
    // need to "parse" and replace the placeholders on our own.
    //
    // Assumptions:
    // - Argument placeholder tokens are named as :db_condition_placeholder_[x]
    //   and Views where condition are in the same order and as the
    //   placeholders.
    $replace = array();
    // Process SQL args in Views JOINs.
    $i = 0;
    foreach ($query->table_queue as $t) {
      if (!empty($t['join']->extra)) {
        foreach ($t['join']->extra as $extra) {
          if (!empty($extra['value'])) {
            $replace[":views_join_condition_{$i}"] = $this->sphinxPrepareSQLArg($extra['value']);
            $i++;
          }
        }
      }
    }
    // Process SQL args in Views WHEREs
    $i = 0;
    foreach ($query->where as $w) {
      if (isset($w['conditions'])) {
        foreach ($w['conditions'] as $condition) {
          if (!empty($condition['value'])) {
            $args = array();
            foreach ($condition['value'] as $arg) {
              $args[] = $this->sphinxPrepareSQLArg($arg);
            }
            $replace[":db_condition_placeholder_{$i}"] = implode(',', $args);
            $i++;
          }
        }
      }
    }
    $sql = strtr($sql, $replace);
    return $sql;
  }

  /**
   * Prepare an SQL argument for use in an SQL query.
   * Do *NOT* use this unless you absolutely know how and why you might need
   * it. It is only used in the Searchlight sphinx backend to generate select
   * queries for use in the sphinx.conf file.
   */
  function sphinxPrepareSQLArg($arg) {
    $connection = Database::getConnection('default');
    switch ($connection->databaseType()) {
      case 'mysql':
      case 'pgsql':
        // @TODO better handling of types. Currently numeric strings and actual
        // number fields can be confused.
        if (is_numeric($arg)) {
        }
        else {
          // addslashes() is used here because mysql_real_escape_string()
          // requires a mysql_connect() connection which is not used by PDO.
          // @TODO: fix this stopgap with a real solution..
          $arg = addslashes($arg);
          $arg = "'{$arg}'";
        }
        return $arg;
    }
  }

  /**
   * Get the ordinal integer for a given string, or vice versa.
   */
  function sphinxGetOrdinal($datasource, $attribute_name, $value, $reset = FALSE) {
    $datasource_id = $datasource->id;

    if (!isset($this->ordinals[$datasource_id]) || $reset) {
      $cid = "searchlight_sphinx_ordinals_{$datasource_id}";
      if (!$reset && $cache = cache_get($cid)) {
        $this->ordinals[$datasource_id] = $cache->data;
      }
      if ($reset || !isset($this->ordinals[$datasource_id])) {
        $mapping = $this->sphinxGetOrdinals($datasource);
        if (!empty($mapping)) {
          cache_set($cid, $mapping);
        }
        $this->ordinals[$datasource_id] = $mapping;
      }
    }
    if (is_numeric($value)) {
      return isset($this->ordinals[$datasource_id][$attribute_name]) ? array_search($value, $this->ordinals[$datasource_id][$attribute_name]) : FALSE;
    }
    else {
      return isset($this->ordinals[$datasource_id][$attribute_name][$value]) ? $this->ordinals[$datasource_id][$attribute_name][$value] : FALSE;
    }
  }

  /**
   * Get an ordinal mapping for this datasource.
   * As this is a very expensive operation, its results are cached and
   * used in the ->sphinxGetOrdinal() method. You should only call this method
   * if you know what you are doing.
   */
  function sphinxGetOrdinals($datasource) {
    $datasource->init();

    // Retrieve all ordinal attributes.
    $attributes = array();
    if (!empty($datasource->fields)) {
      foreach ($datasource->fields as $name => $field) {
        if ($field['datatype'] === 'text' && $field['usage'] === 'attribute') {
          $attributes[$name] = $field;
        }
      }
    }

    // If there are attributes with ordinals, generate a mapping.
    // We only handle string ordinals for sets of less than 1000 (for now).
    $ordinals = array();
    if (!empty($attributes)) {
      $this->sphinxIncludeClient();
      $sphinx = new SphinxClient();
      $sphinx->SetLimits(0, 1000);

      foreach ($attributes as $name => $field) {
        // Dispatch a Sphinx query to retrieve the ordinal.
        $sphinx->SetGroupBy($name, SPH_GROUPBY_ATTR, '@count desc');
        $sphinx->SetGroupDistinct($name);
        $result = $sphinx->Query(NULL, $this->sphinxGetIndex($datasource));

        if (!empty($result['matches']) && count($result['matches']) < 1000) {
          // Dispatch a Views query to retrieve the corresponding string.
          $ids = array_keys($result['matches']);
          $view = $datasource->view->copy();
          $view->build();
          $view->set_items_per_page(0);
          $view->query->where = array();
          $view->query->add_where(0, "{$view->base_table}.{$view->base_field}", $ids, 'IN');
          $view->build_info['query'] = $view->query->query();
          $view->execute();
          foreach ($view->result as $row) {
            $id = $row->{$view->base_field};
            if (isset($result['matches'][$id], $row->{$name})) {
              $ordinals[$name][$row->{$name}] = $result['matches'][$id]['attrs'][$name];
            }
          }
        }
      }
    }
    return $ordinals;
  }

  /**
   * Convert a Sphinx datestring into a timestamp. Requires a datestring
   * (e.g. 201010) and a target granularity: day, month, year. We reverse-apply
   * Drupal's timezone offset so that this date can be used with format_date(),
   * etc. cleanly.
   */
  function sphinxGetTimestamp($datestring, $granularity) {
    global $user;
    if (variable_get('configurable_timezones', 1) && $user->uid && strlen($user->timezone)) {
      $timezone = $user->timezone;
    }
    else {
      $timezone = variable_get('date_default_timezone', 0);
    }
    switch ($granularity) {
      case 'day':
        return ($timezone * -1) + mktime(0, 0, 0, substr($datestring, 4, 2), substr($datestring, 6, 2), substr($datestring, 0, 4));
      case 'month':
        return ($timezone * -1) + mktime(0, 0, 0, substr($datestring, 4, 2), 1, substr($datestring, 0, 4));
      case 'year':
        return ($timezone * -1) + mktime(0, 0, 0, 1, 1, $datestring);
    }
  }

  /**
   * Return the index to query.
   */
  function sphinxGetIndex($datasource) {
    $index = $datasource->id;
    // Determine if we need to query a delta and main index.
    if (!empty($this->settings['delta_ttl'])) {
      $index .= ";{$index}_delta";
    }
    return $index;
  }

}
