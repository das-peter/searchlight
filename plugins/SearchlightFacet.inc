<?php

/**
 * Base Facet plugin class.
 */
class SearchlightFacet {
  var $name;
  var $field;
  var $value;
  var $options;
  var $environment;

  // Render infrastructure objects.
  var $view;
  var $handler;

  /**
   * Constructor.
   */
  function construct($environment, $field, $value, $options) {
    $this->name = $field['name'];
    $this->field = $field;
    $this->value = $value;
    $this->options = $options + $this->optionsDefault();
    $this->environment = $environment;

    $this->view = NULL;
    $this->handler = NULL;
  }

  /**
   * Query method for altering a current page view if this current facet is
   * active. Example: set a filter for this facet's value.
   * Note that $query->add_where() is **NOT** used as this may force
   * undesirable joins on the SQL version of the query.
   */
  function query(&$query) {
    // Filter the query if there is an active facet value.
    if (isset($this->value)) {
      $query->search_filter[] = array(
        'field' => $this->name,
        'operator' => '=',
        'args' => array($this->value),
      );
    }

    // Add this facet to be built by the backend.
    $limit = isset($this->options['items']) ? $this->options['items'] : 5;
    $query->add_search_facet($this->name, $limit);
  }

  /**
   * Generate a label for this facet.
   */
  function label($delta) {
    if (!empty($this->options['label'])) {
      return $this->options['label'];
    }
    else if (isset($this->handler)) {
      return $this->handler->label();
    }
    return '';
  }

  /**
   * Render all items of a facet.
   *
   * @param $query
   *   A Views searchlight query object.
   * @param $delta
   *   The facet block delta, either 'active' or 'facets'. Plugins should
   *   typically show only the active facet value when $delta is 'active' and
   *   only show available facets in the 'facets' block when there is no
   *   active value.
   * @return
   *   An array of facet items with the following keys:
   *
   *     id: The identifier for this facet. eg: 5 (nid), 'blog' (node type)
   *     count: Number of items in resultset that belong to this facet. eg: 20
   *     title: The rendered title for this facet item. eg: 'Blog post'
   */
  function render($query, $delta) {
    switch ($delta) {
      case 'active':
        if (isset($this->value)) {
          $items = array($this->value => array('id' => $this->value));
        }
        break;
      case 'facets':
        if (!isset($this->value)) {
          $items = $query->get_search_facet($this->name);
        }
        break;
    }
    if (!empty($items) && $this->viewInit($query)) {
      return $this->viewRenderItems($items);
    }
    return array();
  }

  /**
   * Theme a single facet item array as a formatted and linked facet item.
   *
   * @param $item
   *   A facet item array as generated by the render() method.
   * @param $delta
   *   The facet block delta, either 'active' or 'facets'.
   * @return
   *   Themed facet item.
   */
  function theme($item, $delta) {
    switch ($delta) {
      case 'active':
        $path = $this->environment->getURLPath();
        $options = $this->environment->getURLOptions('remove', $this->name, $item['id']);
        $item['link'] = l(t('remove'), $path, $options);
        return theme('searchlight_facet_active', array('field' => $this->field, 'item' => $item));
      case 'facets':
        $path = $this->environment->getURLPath();
        $options = $this->environment->getURLOptions('add', $this->name, $item['id']);
        $item['link'] = l($item['title'], $path, $options);
        return theme('searchlight_facet_link', array('field' => $this->field, 'item' => $item));
    }
  }

  /**
   * Initialize and build the View & field handler which will be used for
   * rendering this facet.
   */
  function viewInit($query) {
    if ($this->field['usage'] === 'multivalue') {
      $this->view = searchlight_build_view($this->field['view']);
      foreach ($this->view->field as $field_handler) {
        if (in_array($field_handler->field_alias, array($this->field['name'], $this->field['label_field']['name']), TRUE)) {
          $this->handler = $field_handler;
          break;
        }
      }
    }
    else {
      $this->view = $query->datasource->view->copy();
      $this->view->build();
      foreach ($this->view->field as $field_handler) {
        if ($field_handler->field_alias === $this->name) {
          $this->handler = $field_handler;
        }
      }
    }
    return isset($this->handler, $this->view);
  }

  /**
   * Render the titles of a set of facet items using the View & field handler
   * inited in viewInit().
   */
  function viewRenderItems($items) {
    $rows = array();

    // Multivalue fields must build a new Views query in order to
    // retrieve any secondary label field values.
    if ($this->field['usage'] === 'multivalue') {
      $ids = array_keys($items);

      $query = views_get_plugin('query', 'views_query');
      $query->init($this->field['table'], $this->field['field']);

      // Add WHERE condition on item ids.
      $query->add_where(0, "{$this->field['table']}.{$this->field['field']}", $ids, 'IN');

      // Add base field.
      $field_alias = $query->add_field($this->field['table'], $this->field['field']);

      // Add label field.
      $label_alias = $query->add_field($this->field['label_field']['table'], $this->field['label_field']['field']);

      $result = db_query($query->query(), $ids);
      $rows = array();
      while ($row = db_fetch_object($result)) {
        $row->{$this->name} = $row->{$this->field['field']};
        $row->{$this->field['label_field']['name']} = $row->{$label_alias};
        $rows[$row->{$this->name}] = $row;
      }
    }
    // For regular fields attempt to spoof rows with the appropriate field
    // values for rendering by the field handler.
    else {
      $rows = array();
      foreach ($items as $item) {
        $row = new stdClass();
        $row->{$this->name} = $item['id'];
        $rows[$item['id']] = $row;
      }
    }
    // Render item titles.
    $this->handler->pre_render($rows);
    foreach ($rows as $id => $row) {
      $items[$id]['title'] = strip_tags($this->handler->render($row));
    }
    return $items;
  }

  /**
   * Provide default values for options.
   */
  function optionsDefault() {
    return array(
      'label' => '',
      'items' => 5,
    );
  }

  /**
   * Provide an options form to be exposed in the Environment editor.
   */
  function optionsForm(&$form, $form_state) {
    $form['label'] = array(
      '#title' => t('Label'),
      '#type' => 'textfield',
      '#size' => 20,
      '#default_value' => $this->options['label'],
    );
    $form['items'] = array(
      '#title' => t('Items to show'),
      '#type' => 'select',
      '#default_value' => $this->options['items'],
      '#options' => drupal_map_assoc(range(1, 20)) + array(0 => t('Show all')),
    );
  }

  /**
   * Provide extended form elements for a facet in the Environment editor.
   */
  function extendedForm(&$form, $form_state) {
  }
}
