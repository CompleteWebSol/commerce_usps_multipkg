<?php

/**
 * @file
 * Handles rate request/response related stuff for the Commerce USPS module.
 */

/**
 * Builds a domestics USPS rate request.
 *
 * @param object $order
 *   The commerce order object.
 * @param object $shipping_address
 *   The commerce_customer_address array of the shipping profile.
 *
 * @return array
 *   An array of shipping rates.
 */
function commerce_usps_multipkg_rate_v4_request($order, $shipping_address) {
  $rates = array();
  $usps_services = commerce_usps_multipkg_service_list('domestic');

//  $weight = commerce_usps_multipkg_get_order_weight($order);
  $packages = _commerce_usps_multipkg_get_packages($order);

  $request = new SimpleXMLElement('<RateV4Request/>');
  $request->addAttribute('USERID', variable_get('commerce_usps_multipkg_user', ''));
  $request->addChild('Revision', 2);

  // @TODO: Support multiple packages based on physical attributes.
  // Add a package to the request for each enabled service.
  $i = 1;

  foreach (variable_get('commerce_usps_multipkg_services', array()) as $machine_name => $service) {
    foreach ($packages as $k => $pkg) {
      $package = $request->addChild('Package');
      $package->addAttribute('ID', $i);
      $package->addChild('Service', $usps_services[$machine_name]['request_name']);
      $package->addChild('FirstClassMailType', 'PARCEL');
      $package->addChild('ZipOrigination', substr(variable_get('commerce_usps_multipkg_postal_code', ''), 0, 5));
      $package->addChild('ZipDestination', substr($shipping_address['postal_code'], 0, 5));
      $package->addChild('Pounds', $pkg['weight']['pounds']); //$weight['pounds']);
      $package->addChild('Ounces', $pkg['weight']['ounces']); //$weight['ounces']);
      $package->addChild('Container', 'VARIABLE');
      $package->addChild('Size', 'REGULAR'); //$pkg['size']); //'REGULAR');
      $package->addChild('Width', $pkg['dimensions']['width']); //'');
      $package->addChild('Length', $pkg['dimensions']['length']); //'');
      $package->addChild('Height', $pkg['dimensions']['height']); //'');
      $package->addChild('Girth', $pkg['dimensions']['girth']); //'');
      $package->addChild('Machinable', 'TRUE');
      $i++;
    }
  }

  drupal_alter('commerce_usps_multipkg_rate_v4_request', $request);

  // Submit the rate request to USPS.
  $response = commerce_usps_multipkg_api_request('API=RateV4&XML=' . $request->asXML());

  if (!empty($response->Package)) {
    // Loop through each of the package results to build the rate array.
    foreach ($response->Package as $package) {
      if (empty($package->Error)) {
        // Load the shipping service's class id from the package response.
        $id = (string) $package->Postage->attributes()->{'CLASSID'};

        // Look up the shipping service by it's class id.
        $usps_service = commerce_usps_multipkg_service_by_id($id, 'domestic');

        // Make sure that the package service is registered.
        if (!empty($usps_service['machine_name'])) {
          $rates[$usps_service['machine_name']] = array(
            'amount' => commerce_currency_decimal_to_amount((string) $package->Postage->Rate, commerce_default_currency()),
            'currency_code' => commerce_default_currency(),
            'data' => array(),
          );
        }
      }
    }
  }
  return $rates;
}

/**
 * Builds an international USPS rate request.
 *
 * @param object $order
 *   The commerce order object.
 * @param array $shipping_address
 *   The commerce_customer_address array of the shipping profile.
 *
 * @return array
 *   An array of shipping rates.
 */
function commerce_usps_multipkg_intl_rate_v2_request($order, $shipping_address) {
  $rates = array();

//  $weight = commerce_usps_multipkg_get_order_weight($order);
  $packages = _commerce_usps_multipkg_get_packages($order);

  $request = new SimpleXMLElement('<IntlRateV2Request/>');
  $request->addAttribute('USERID', variable_get('commerce_usps_multipkg_user', ''));
  $request->addChild('Revision', 2);
  $shipment_value = commerce_usps_multipkg_get_shipment_value($order);

  // @TODO: Support multiple packages based on physical attributes.
  foreach ($packages as $k => $pkg) {
    $package = $request->addChild('Package');
    $package->addAttribute('ID', 1);
    $package->addChild('Pounds', $pkg['weight']['pounds']); //$weight['pounds']);
    $package->addChild('Ounces', $pkg['weight']['ounces']); //$weight['ounces']);
    $package->addChild('Machinable', 'True');
    $package->addChild('MailType', 'Package');
    $package->addChild('ValueOfContents', commerce_currency_amount_to_decimal($shipment_value, commerce_default_currency()));
    $package->addChild('Country', commerce_usps_multipkg_country_get_predefined_list($shipping_address['country']));
    $package->addChild('Container', 'RECTANGULAR');
    $package->addChild('Size', 'REGULAR'); //$pkg['size']); //'REGULAR');
    $package->addChild('Width', $pkg['dimensions']['width']); //'');
    $package->addChild('Length', $pkg['dimensions']['length']); //'');
    $package->addChild('Height', $pkg['dimensions']['height']); //'');
    $package->addChild('Girth', $pkg['dimensions']['girth']); //'');
    $package->addChild('OriginZip', substr(variable_get('commerce_usps_multipkg_postal_code', ''), 0, 5));
    $package->addChild('CommercialFlag', 'N');
  }
  drupal_alter('commerce_usps_multipkg_intl_rate_v2_request', $request);

  // Submit the rate request to USPS.
  $response = commerce_usps_multipkg_api_request('API=IntlRateV2&XML=' . $request->asXML());

  if (!empty($response->Package->Service)) {
    foreach ($response->Package->Service as $service) {
      $id = (string) $service->attributes()->{'ID'};

      // Look up the shipping service by it's id.
      $usps_service = commerce_usps_multipkg_service_by_id($id, 'international');

      // Make sure that the package service is registered.
      if (!empty($usps_service['machine_name'])) {
        $rates[$usps_service['machine_name']] = array(
          'amount' => commerce_currency_decimal_to_amount((string) $service->Postage, commerce_default_currency()),
          'currency_code' => commerce_default_currency(),
          'data' => array(),
        );
      }
    }
  }

  return $rates;
}

/**
 * Submits an API request to USPS.
 *
 * @param string $request
 *   A request string.
 * @param string $message
 *   Optional log message.
 *
 * @return string
 *   XML string response from USPS
 */
function commerce_usps_multipkg_api_request($request, $message = '') {

  commerce_usps_multipkg_log('Submitting API request to USPS. @message:<pre>@request</pre>', array('@message' => $message, '@request' => $request));

  $request_url = variable_get('commerce_usps_multipkg_connection_address', 'http://Production.ShippingAPIs.com/ShippingAPI.dll');

  // Send the request.
  $response = drupal_http_request($request_url, array('method' => 'POST', 'data' => $request));

  // If we received a response.
  if (!empty($response->data)) {
    commerce_usps_multipkg_log('Response code:@code<br />Response:<pre>@response</pre>', array('@code' => $response->code, '@response' => $response->data));
    return new SimpleXMLElement($response->data);
  }
  else {
    $settings_url = l(t('settings'), 'admin/commerce/config/shipping/methods/usps/edit');
    commerce_usps_multipkg_log('We did not receive a response from USPS. Make sure you have the correct server url in your !settings.', array('!settings' => $settings_url), WATCHDOG_ERROR);
  }

  return FALSE;
}

function _commerce_usps_multipkg_get_packages($order) {
  $flatRate = 0;
  $freeShipping = false;
  $total_package_count = 0;
  $total_package_weight = 0;
  $order_wrapper = entity_metadata_wrapper('commerce_order', $order);
  // We're going to loop through line items and determine weights and dims.
  // If an item has single dims and will fit in default package, then try to add to 'open package'
  // If it would fit, but too full or too heavy, close package & start new one.
  // If items has multiple dims, the each one is it's own package.
  
  $defaultLength = variable_get('commerce_ups_multipkg_default_package_size_length', '0');
  $defaultWidth = variable_get('commerce_ups_multipkg_default_package_size_width', '0');
  $defaultHeight = variable_get('commerce_ups_multipkg_default_package_size_height', '0');
  $default_package_volume = $defaultLength * $defaultWidth * $defaultHeight;
  $packages = array();
  $currentVolume = 0;
  $currentWeight = 0;
  $currentCount = 0;
  // Cycle through line items
  foreach ($order_wrapper->commerce_line_items as $delta => $line_item_wrapper) {
    if (in_array($line_item_wrapper->type->value(), commerce_product_line_item_types())) {
      // Verify line item is a 'product' and has data
      if (!empty($line_item_wrapper->commerce_product)) {
        $product = $line_item_wrapper->commerce_product->value();
        // Get field collections
        $collections = array();
        $package_dims = array();
        // Free shipping?
        if(isset($product->field_free_shipping) && count($product->field_free_shipping) > 0 && $product->field_free_shipping[LANGUAGE_NONE][0]['value']=="1") {
          // Free shipping - skip
          $freeShipping=true;
          continue;
        }
        if(isset($product->field_flat_rate_shipping[LANGUAGE_NONE][0]['value']) && $product->field_flat_rate_shipping[LANGUAGE_NONE][0]['value']==1) {
          $flatRate += ($line_item_wrapper->quantity->value() * $product->field_flat_rate_amount[LANGUAGE_NONE][0]['value']);
          continue;
        }
        // Flat rate shipping?
        
        foreach ($product->field_product_packaging[LANGUAGE_NONE] as $delta => $collection) {
          $collections[] = $collection['value'];
        }
        
        
        // loop for quantity
        $line_qty = $line_item_wrapper->quantity->value();
        for($qty=0; $qty<$line_qty;$qty++) {
          $package_collections = field_collection_item_load_multiple($collections);
          
          
          if (count($package_collections) == 1) {
            // Single dimension set - check for package combining
            $package_collections = reset($package_collections);
            $box_dims = $package_collections->field_box_dimensions[LANGUAGE_NONE][0];
            $box_weight = $package_collections->field_box_weight[LANGUAGE_NONE][0];
            $pkg_vol = $box_dims['length'] *
              $box_dims['width'] *
              $box_dims['height'];
            // Will it fit inside a box?
            if ($pkg_vol <= $default_package_volume) {
              // Will it fit in current box ?
              if ($currentVolume + $pkg_vol <= $default_package_volume) {
                $currentVolume += $pkg_vol;
                $currentWeight += $box_weight['weight'];
                $currentCount++;  // Keep track of content count
              }
              else {
                // No, close and start new package
                // We didn't actually keep track of how many items are in 'current box',
                // So we assume more than one and use default pkg size
                $lbs = floor($currentWeight);
                $oz = $currentWeight - $lbs;
                $oz = ceil($oz*16);
                $girth = $box_dims['width'] * 2 + $box_dims['height'] * 2;
                if($box_dims['width']>12 || $box_dims['height']>12 || $box_dims['length']>12) {
                  $size = "LARGE";
                } else {
                  $size = "REGULAR";
                }
                $packages[] = array(
                  'weight' => array(
                    'weight' => $currentWeight,
                    'unit' => 'lb',
                    'pounds' => $lbs,
                    'ounces' => $oz,
                  ),
                  'dimensions' => array(
                    'length' => $defaultLength,
                    'width' => $defaultWidth,
                    'height' => $defaultHeight,
                    'volume' => $default_package_volume,
                    'unit' => 'in'
                  ),
                  'size' => $size,
                );
                $total_package_count++;
                $total_package_weight+=$currentWeight;
                // Previous package closed, start new one
                $currentVolume = $pkg_vol;
                $currentWeight = $box_weight['weight'];
                $currentCount = 0;
              }
            }
            else {
              // No, item is seperate package
              $currentVolume += $pkg_vol;
              $currentWeight += $box_weight['weight'];
              $lbs = floor($currentWeight);
              $oz = $currentWeight - $lbs;
              $oz = ceil($oz*16);
              $girth = $box_dims['width'] * 2 + $box_dims['height'] * 2;
              if($box_dims['width']>12 || $box_dims['height']>12 || $box_dims['length']>12) {
                $size = "LARGE";
              } else {
                $size = "REGULAR";
              }
              $packages[] = array(
                'weight' => array(
                  'weight' => $currentWeight,
                  'unit' => 'lb',
                  'pounds' => $lbs,
                  'ounces' => $oz,
                ),
                'dimensions' => $box_dims + array('volume' => $pkg_vol, 'girth' => $girth),
                'size' => $size,
              );
              
              $total_package_count++;
              $total_package_weight+=$box_weight['weight'];
            }
          }
          else {
            // Multiple packages always treated independently
            foreach ($package_collections as $pkg) {
              $box_dims = $pkg->field_box_dimensions[LANGUAGE_NONE][0];
              $pkg_vol = $pkg->field_box_dimensions[LANGUAGE_NONE][0]['length'] *
                $pkg->field_box_dimensions[LANGUAGE_NONE][0]['width'] *
                $pkg->field_box_dimensions[LANGUAGE_NONE][0]['height'];
              
              $lbs = floor($pkg->field_box_weight[LANGUAGE_NONE][0]['weight']);
              $oz = $pkg->field_box_weight[LANGUAGE_NONE][0]['weight'] - $lbs;
              $oz = ceil($oz*16);
              $girth = $box_dims['width'] * 2 + $box_dims['height'] * 2;
              if($box_dims['width']>12 || $box_dims['height']>12 || $box_dims['length']>12) {
                $size = "LARGE";
              } else {
                $size = "REGULAR";
              }
              $packages[] = array(
                'weight' => array(
                  'weight' => $pkg->field_box_weight[LANGUAGE_NONE][0]['weight'],
                  'unit' => $pkg->field_box_weight[LANGUAGE_NONE][0]['unit'],
                  'pounds' => $lbs,
                  'ounces' => $oz,
                ),
//              $packages[] = array(
//                'weight' => array(
//                  'weight' => $pkg->field_box_weight[LANGUAGE_NONE][0]['weight'],
//                  'unit' => $pkg->field_box_weight[LANGUAGE_NONE][0]['unit'],
//                ),
                'dimensions' => array(
                  'length' => $pkg->field_box_dimensions[LANGUAGE_NONE][0]['length'],
                  'width' => $pkg->field_box_dimensions[LANGUAGE_NONE][0]['width'],
                  'height' => $pkg->field_box_dimensions[LANGUAGE_NONE][0]['height'],
                  'volume' => $pkg_vol,
                  'unit' => $pkg->field_box_dimensions[LANGUAGE_NONE][0]['unit'],
                  'girth' => $girth,
                ),
                'size' => $size,
              );
              $total_package_count++;
              $total_package_weight+=$pkg->field_box_weight[LANGUAGE_NONE][0]['weight'];
            }
            // Packages added to list array.
          }
          // now process packages
        }
      }
    }
  }
  // Now add 'current package' if it has something in it.
  if ($currentWeight > 0) {
    $lbs = floor($currentWeight);
    $oz = $currentWeight - $lbs;
    $oz = ceil($oz*16);
    $girth = $box_dims['width'] * 2 + $box_dims['height'] * 2;
    if($box_dims['width']>12 || $box_dims['height']>12 || $box_dims['length']>12) {
      $size = "LARGE";
    } else {
      $size = "REGULAR";
    }
    $packages[] = array(
      'weight' => array(
        'weight' => $currentWeight,
        'unit' => 'lb',
        'pounds' => $lbs,
        'ounces' => $oz,
      ),
      'dimensions' => array(
        'length' => $defaultLength,
        'width' => $defaultWidth,
        'height' => $defaultHeight,
        'volume' => $default_package_volume,
        'unit' => 'in'
      ),
      'size' => $size,
    );
    
    $total_package_count++;
    $total_package_weight+=$currentWeight;
  }
  $order->order_packages = array('count'=>$total_package_count,'weight'=>$total_package_weight);
  // Is there a partial package going?
  
  //    dpm($packages);
  // we can eliminate and reference array directly.
  $number_of_packages = count($packages);
  return $packages;
}
